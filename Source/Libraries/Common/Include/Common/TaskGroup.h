#pragma once

// Common
#include "Dispatcher.h"
#include "DispatcherBucket.h"

// Std
#include <atomic>

/// User functor
using TaskGroupFunctor = Delegate<void(DispatcherBucket* bucket, void* userData)>;

class TaskGroup {
public:
    TaskGroup(Dispatcher* dispatcher) {
        controller = new (dispatcher->allocators) Controller(dispatcher);
    }

    ~TaskGroup() {
        controller->Detach();
    }

    /// No copy or move
    TaskGroup(const TaskGroup& other) = delete;
    TaskGroup(TaskGroup&& other) = delete;

    /// No copy or move assignment
    TaskGroup& operator=(const TaskGroup& other) = delete;
    TaskGroup& operator=(TaskGroup&& other) = delete;

    /// Chain a task
    /// \param delegate the task delegate
    /// \param userData optional, the user data for the task
    void Chain(const TaskGroupFunctor& delegate, void* userData) {
        controller->AddLink(delegate, userData);
    }

    /// Commit all tasks
    void Commit() {
        controller->Commit();
    }

private:
    struct Controller {
        struct LinkData {
            TaskGroupFunctor functor;
            void* userData;
            DispatcherBucket* bucket;
        };

        Controller(Dispatcher* dispatcher) : dispatcher(dispatcher) {
            bucket.userData = nullptr;
            bucket.functor = BindDelegate(this, Controller::OnLinkCompleted);
            bucket.SetCounter(1);
        }

        /// Invoked once a link has been completed
        void OnLinkCompleted(void*) {
            // Out of jobs?
            mutex.lock();
            if (jobs.empty()) {
                mutex.unlock();

                // Release the controller
                Release();
                return;
            }

            // Mark the primary job as needing counting
            bucket.SetCounter(1);

            // Pop the job
            DispatcherJob job = jobs.front();
            jobs.pop_front();

            // Submit!
            dispatcher->Add(job);
            mutex.unlock();
        }

        /// Link entry point
        void LinkEntry(void* data) {
            auto* linkData = static_cast<LinkData*>(data);
            linkData->functor.Invoke(linkData->bucket, linkData->userData);
            destroy(linkData, dispatcher->allocators);
        }

        /// Add a link to the controller
        /// \param delegate the task delegate
        /// \param userData optional, the user data for the task
        void AddLink(const TaskGroupFunctor& delegate, void* userData) {
            std::lock_guard lock(mutex);

            auto linkData = new (dispatcher->allocators) LinkData;
            linkData->functor = delegate;
            linkData->userData = userData;
            linkData->bucket = &bucket;

            jobs.push_back(DispatcherJob {
                .userData = linkData,
                .delegate = BindDelegate(this, Controller::LinkEntry),
                .bucket = &bucket
            });

            anyLinks = true;
        }

        /// Commit all jobs
        void Commit() {
            // Ignore if empty
            if (jobs.empty()) {
                return;
            }

            /// Pop the first job
            DispatcherJob job = jobs.front();
            jobs.pop_front();

            // Submit!
            dispatcher->Add(job);
        }

        /// Detach from the TaskGroup
        void Detach() {
            // If no links have been submitted, release the controller
            if (!anyLinks) {
                Release();
            }
        }

        /// Destruct this controller
        void Release() {
            destroy(this, dispatcher->allocators);
        }

        std::mutex mutex;
        Dispatcher* dispatcher{nullptr};
        std::list<DispatcherJob> jobs;
        bool anyLinks{false};
        DispatcherBucket bucket;
    };

    Controller* controller{nullptr};
};