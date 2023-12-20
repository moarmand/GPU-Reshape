#include <Test/Automation/Connection/PoolingListener.h>

void PoolingListener::Handle(const MessageStream* streams, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        std::lock_guard guard(mutex);

        // Get stream
        const MessageStream& stream = streams[i];

        // Commit for the entire schema
        Commit(stream.GetSchema(), stream.GetDataBegin(), stream.GetByteSize(), stream.GetCount());

        // If ordered, commit for all the individual messages within
        if (stream.GetSchema().type == MessageSchemaType::Ordered) {
            for (auto it = ConstMessageStreamView(stream).GetIterator(); it; ++it) {
                Commit(MessageSchema {
                    .type = MessageSchemaType::Ordered,
                    .id = it.GetID()
                }, it.ptr, it.GetByteSize(), 1u);
            }
        }
    }
}

void PoolingListener::Register(MessageSchema schema, MessageTask* task) {
    std::lock_guard guard(mutex);
    poolingSchemas[schema].tasks.Add(task);

    // Assign controller
    task->controller = controllerPool.Pop();
    task->controller->pendingRelease = false;

    // Set commit head
    task->acquiredId = task->controller->commitId;
}

void PoolingListener::Deregister(MessageSchema schema, MessageTask* task) {
    std::lock_guard guard(mutex);
    poolingSchemas[schema].tasks.Remove(task);

    // Mark as released
    task->MarkAsReleased();

    // Move controller to shared pool
    controllerPool.Push(task->controller);
}

void PoolingListener::Transfer(MessageSchema schema, MessageTask* from, MessageTask* to) {
    std::lock_guard guard(mutex);
    poolingSchemas[schema].tasks.Remove(from);
    poolingSchemas[schema].tasks.Add(to);

    // Mark source as released
    from->MarkAsReleased();

    // Move controller
    to->controller = from->controller;
}

void PoolingListener::Commit(MessageSchema schema, const void* ptr, size_t length, uint32_t count) {
    auto schemaIt = poolingSchemas.find(schema);
    if (schemaIt != poolingSchemas.end()) {
        Commit(schemaIt->second, ptr, length, count);
    }
}

void PoolingListener::Commit(TaskBucket& bucket, const void* ptr, size_t length, uint32_t count) {
    for (MessageTask* task : bucket.tasks) {
        Commit(task, ptr, length, count);
    }

    // Cleanup dead tasks
    bucket.tasks.RemoveIf([&](MessageTask* task) {
        return task->controller->pendingRelease;
    });
}

void PoolingListener::Commit(MessageTask* task, const void* ptr, size_t length, uint32_t count) {
    std::lock_guard guard(task->controller->mutex);

    // Move all dadta
    task->stream.SetData(ptr, length, count);

    // Mark for pending release if needed
    if (task->ShouldReleaseOnAcquire()) {
        task->controller->pendingRelease = true;
        controllerPool.Push(task->controller);
    }

    // Move the controller forward, notify a single thread
    // Controllers wait dependencies are single threaded at most, for now at least
    task->controller->commitId++;
    task->controller->wakeCondition.notify_one();
}
