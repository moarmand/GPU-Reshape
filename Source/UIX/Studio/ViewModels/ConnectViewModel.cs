﻿// 
// The MIT License (MIT)
// 
// Copyright (c) 2024 Advanced Micro Devices, Inc.,
// Fatalist Development AB (Avalanche Studio Group),
// and Miguel Petersen.
// 
// All Rights Reserved.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files (the "Software"), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
// of the Software, and to permit persons to whom the Software is furnished to do so, 
// subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all 
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR 
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE 
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, 
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// 

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Drawing;
using System.Linq;
using System.Reactive.Disposables;
using System.Reactive.Linq;
using System.Text.RegularExpressions;
using System.Windows.Input;
using Avalonia;
using Avalonia.Media;
using Avalonia.Threading;
using Bridge.CLR;
using DynamicData;
using Message.CLR;
using ReactiveUI;
using Runtime.Models.Query;
using Studio.Extensions;
using Studio.Models.Workspace;
using Studio.Services;
using Studio.Services.Suspension;
using Studio.ViewModels.Query;
using Studio.ViewModels.Workspace;
using Color = Avalonia.Media.Color;

namespace Studio.ViewModels
{
    public class ConnectViewModel : ReactiveObject, IBridgeListener, IActivatableViewModel
    {
        /// <summary>
        /// Connect to selected application
        /// </summary>
        public ICommand Connect { get; }

        /// <summary>
        /// VM activator
        /// </summary>
        public ViewModelActivator Activator { get; } = new();

        /// <summary>
        /// Current connection string
        /// </summary>
        [DataMember]
        public string ConnectionString
        {
            get => _connectionString;
            set => this.RaiseAndSetIfChanged(ref _connectionString, value);
        }

        /// <summary>
        /// Parsed connection query
        /// </summary>
        public ConnectionQueryViewModel? ConnectionQuery
        {
            get => _connectionQuery;
            set => this.RaiseAndSetIfChanged(ref _connectionQuery, value);
        }

        /// <summary>
        /// All resolved applications
        /// </summary>
        public ObservableCollection<ApplicationInfoViewModel> ResolvedApplications => _resolvedApplications;

        /// <summary>
        /// User interaction during connection acceptance
        /// </summary>
        public Interaction<ConnectViewModel, bool> AcceptClient { get; }

        /// <summary>
        /// Selected application for connection
        /// </summary>
        public ApplicationInfoViewModel? SelectedApplication
        {
            get => _selectedApplication;
            set => this.RaiseAndSetIfChanged(ref _selectedApplication, value);
        }

        /// <summary>
        /// Connection status
        /// </summary>
        public Models.Workspace.ConnectionStatus ConnectionStatus
        {
            get => _connectionStatus;
            set => this.RaiseAndSetIfChanged(ref _connectionStatus, value);
        }

        /// <summary>
        /// All string decorators
        /// </summary>
        public SourceList<QueryAttributeDecorator> QueryDecorators
        {
            get => _queryDecorators;
            set => this.RaiseAndSetIfChanged(ref _queryDecorators, value);
        }

        public ConnectViewModel()
        {
            // Initialize connection status
            ConnectionStatus = Models.Workspace.ConnectionStatus.None;

            // Create commands
            Connect = ReactiveCommand.Create(OnConnect);

            // Create interactions
            AcceptClient = new();

            // Subscribe
            _connectionViewModel.Connected.Subscribe(_ => OnRemoteConnected());

            // Notify on query string change
            this.WhenAnyValue(x => x.ConnectionString)
                .Throttle(TimeSpan.FromMilliseconds(250))
                .ObserveOn(RxApp.MainThreadScheduler)
                .Subscribe(CreateConnectionQuery);

            // Notify on query change
            this.WhenAnyValue(x => x.ConnectionQuery)
                .Throttle(TimeSpan.FromMilliseconds(750))
                .ObserveOn(RxApp.MainThreadScheduler)
                .WhereNotNull()
                .Subscribe(CreateConnection);

            // Cleanup
            this.WhenDisposed(x =>
            {
                // Stop timer
                _timer?.Stop();

                // Register handler
                _connectionViewModel.Bridge?.Deregister(this);
            });
            
            // Suspension
            this.BindTypedSuspension();
        }

        /// <summary>
        /// Refresh the current query
        /// </summary>
        public void RefreshQuery()
        {
            CreateConnectionQuery(ConnectionString);
        }

        /// <summary>
        /// Create a new connection query
        /// </summary>
        /// <param name="query">given query</param>
        private void CreateConnectionQuery(string query)
        {
            // Try to parse collection
            QueryAttribute[]? attributes = QueryParser.GetAttributes(query);
            if (attributes == null)
            {
                ConnectionStatus = ConnectionStatus.QueryInvalid;
                return;
            }

            // Create new segment list
            QueryDecorators.Clear();

            // Visualize segments
            foreach (QueryAttribute attribute in attributes)
            {
                QueryDecorators.Add(new QueryAttributeDecorator()
                {
                    Attribute = attribute,
                    Color = attribute.Key switch
                    {
                        "port" => ResourceLocator.GetResource<Color>("ConnectionKeyPort"),
                        "app" => ResourceLocator.GetResource<Color>("ConnectionKeyApplicationFilter"),
                        "pid" => ResourceLocator.GetResource<Color>("ConnectionKeyApplicationPID"),
                        "api" => ResourceLocator.GetResource<Color>("ConnectionKeyApplicationAPI"),
                        _ => ResourceLocator.GetResource<Color>("ConnectionKeyIP")
                    }
                });
            }

            // Attempt to parse
            ConnectionStatus status = ConnectionQueryViewModel.FromAttributes(attributes, out var queryObject);

            // Update status
            if (status != ConnectionStatus.None)
            {
                ConnectionStatus = status;
            }
            else
            {
                ConnectionStatus = _endpointConnected ? GetEndpointConnectionStatus(ConnectionStatus.EndpointConnected) : ConnectionStatus.None;
            }

            // Set query
            ConnectionQuery = queryObject;

            // Re-filter if needed
            FilterApplications();
        }

        /// <summary>
        /// Create a new connection
        /// </summary>
        /// <param name="query"></param>
        private void CreateConnection(ConnectionQueryViewModel query)
        {
            // Filter recreation of the endpoint connection
            if (_pendingQuery?.IPvX == query.IPvX && _pendingQuery?.Port == query.Port)
            {
                return;
            }
            
            // Empty out pooled applications
            _resolvedApplications.Clear();
            _discoveryApplications.Clear();

            // Not connected yet
            _endpointConnected = false;

            // Start connection
            _connectionViewModel.Connect(query.IPvX switch
            {
                null or "localhost" => "127.0.0.1",
                _ => query.IPvX
            }, query.Port);
            _pendingQuery = query;

            // Set status
            ConnectionStatus = GetEndpointConnectionStatus(ConnectionStatus.ConnectingEndpoint);
        }

        /// <summary>
        /// Connect implementation
        /// </summary>
        private void OnConnect()
        {
            if (SelectedApplication == null)
            {
                return;
            }

            // Quick check that we're not already connected
            if (App.Locator.GetService<Services.IWorkspaceService>() is { } service)
            {
                if (service.Workspaces.Items.Any(x => x.Connection?.Application?.Guid == SelectedApplication?.Guid))
                {
                    // Mark as failure
                    ConnectionStatus = Models.Workspace.ConnectionStatus.ApplicationAlreadyConnected;
                    return;
                }
            }

            // Set status
            ConnectionStatus = Models.Workspace.ConnectionStatus.Connecting;

            // Submit request
            _connectionViewModel.RequestClientAsync(SelectedApplication);
        }

        /// <summary>
        /// Invoked when the remote endpoint has connected
        /// </summary>
        private void OnRemoteConnected()
        {
            // Register handler
            _connectionViewModel.Bridge?.Register(this);

            // Request discovery
            _connectionViewModel.DiscoverAsync();

            Dispatcher.UIThread.InvokeAsync(() =>
            {
                // Existing?
                _timer?.Stop();

                // Create timer on main thread
                _timer = new DispatcherTimer(DispatcherPriority.Background)
                {
                    Interval = TimeSpan.FromSeconds(1),
                    IsEnabled = true
                };

                // Subscribe tick
                _timer.Tick += OnPoolingTick;

                // Must call start manually (a little vague)
                _timer.Start();
            });
        }

        /// <summary>
        /// Invoked on timer pooling
        /// </summary>
        /// <param name="sender"></param>
        /// <param name="e"></param>
        private void OnPoolingTick(object? sender, EventArgs e)
        {
            _connectionViewModel.DiscoverAsync();
        }

        /// <summary>
        /// Message handler
        /// </summary>
        /// <param name="streams">all inbound streams</param>
        /// <param name="count">number of streams</param>
        public void Handle(ReadOnlyMessageStream streams, uint count)
        {
            var schema = new OrderedMessageView(streams);

            // Visit typed
            foreach (OrderedMessage message in schema)
            {
                switch (message.ID)
                {
                    case HostDiscoveryMessage.ID:
                    {
                        Handle(message.Get<HostDiscoveryMessage>());
                        break;
                    }
                    case HostConnectedMessage.ID:
                    {
                        Handle(message.Get<HostConnectedMessage>());
                        break;
                    }
                    case HostResolvedMessage.ID:
                    {
                        Handle(message.Get<HostResolvedMessage>());
                        break;
                    }
                }
            }
        }

        /// <summary>
        /// Invoked on connection messages
        /// </summary>
        /// <param name="connected">message</param>
        private void Handle(HostConnectedMessage connected)
        {
            var flat = connected.Flat;

            Dispatcher.UIThread.InvokeAsync(async () =>
            {
                // Set status
                if (flat.accepted == 0)
                {
                    ConnectionStatus = Models.Workspace.ConnectionStatus.ApplicationRejected;
                }
                else
                {
                    ConnectionStatus = Models.Workspace.ConnectionStatus.ApplicationAccepted;
                }

                // Confirm with view
                if (!await AcceptClient.Handle(this))
                {
                    return;
                }

                // Get provider
                var provider = App.Locator.GetService<Services.IWorkspaceService>();

                // Create workspace
                var workspace = new ViewModels.Workspace.WorkspaceViewModel()
                {
                    Connection = _connectionViewModel
                };
                
                // Configure and register workspace
                provider?.Install(workspace);
                provider?.Add(workspace);
            });
        }

        /// <summary>
        /// Invoked on resolved messages
        /// </summary>
        /// <param name="resolved">message</param>
        private void Handle(HostResolvedMessage resolved)
        {
            var flat = resolved.Flat;

            Dispatcher.UIThread.InvokeAsync(() =>
            {
                // Set status
                if (flat.accepted == 0)
                {
                    ConnectionStatus = Models.Workspace.ConnectionStatus.ResolveRejected;
                }
                else
                {
                    ConnectionStatus = Models.Workspace.ConnectionStatus.ResolveAccepted;
                }
            });
        }

        /// <summary>
        /// Invoked on discovery messages
        /// </summary>
        /// <param name="discovery">message</param>
        private void Handle(HostDiscoveryMessage discovery)
        {
            var schema = new OrderedMessageView(discovery.infos.Stream);

            // Create new app list
            var apps = new List<ApplicationInfoViewModel>();

            // Visit typed
            foreach (OrderedMessage message in schema)
            {
                switch (message.ID)
                {
                    case HostServerInfoMessage.ID:
                    {
                        var info = message.Get<HostServerInfoMessage>();

                        // Add application
                        apps.Add(new ApplicationInfoViewModel
                        {
                            Name = info.application.String,
                            Process = info.process.String,
                            API = info.api.String,
                            Pid = info.processId,
                            DeviceUid = info.deviceUid,
                            DeviceObjects= info.deviceObjects,
                            Guid = new Guid(info.guid.String)
                        });
                        break;
                    }
                }
            }

            // Add on UI thread
            Dispatcher.UIThread.InvokeAsync(() =>
            {
                // Mark as connected
                if (!_endpointConnected)
                {
                    _endpointConnected = true;
                    ConnectionStatus = GetEndpointConnectionStatus(ConnectionStatus.EndpointConnected);
                }

                // Set new app list
                _discoveryApplications.Clear();
                _discoveryApplications.Add(apps);

                // Re-filter
                FilterApplications();
            });
        }

        /// <summary>
        /// Get the endpoint connection status override
        /// </summary>
        private ConnectionStatus GetEndpointConnectionStatus(ConnectionStatus status)
        {
            // Local host?
            if (ConnectionQuery?.IPvX?.Trim() == "localhost")
            {
                // Check discovery, even if the local host connects, it's limited to manual registrations
                if (App.Locator.GetService<IBackendDiscoveryService>()?.Service is { } service && !service.IsRunning())
                {
                    return ConnectionStatus.DiscoveryNotActive;
                }
            }

            // Remote host
            return status;
        }

        /// <summary>
        /// Re-filter all applications
        /// </summary>
        private void FilterApplications()
        {
            if (ConnectionQuery == null)
            {
                return;
            }

            // Filter by current query
            List<ApplicationInfoViewModel> applications = _discoveryApplications.Where(x =>
                {
                    return (ConnectionQuery.ApplicationPid?.Equals((int)x.Pid) ?? true) &&
                           x.API.Contains(ConnectionQuery.ApplicationAPI ?? string.Empty, StringComparison.InvariantCultureIgnoreCase) &&
                           x.Process.Contains(ConnectionQuery.ApplicationFilter ?? string.Empty, StringComparison.InvariantCultureIgnoreCase);
                }).ToList();

            // Create hash lookups
            HashSet<Guid> resolved = new(_resolvedApplications.Select(x => x.Guid));
            HashSet<Guid> incomingGuids = new(applications.Select(x => x.Guid));
            
            // Add or update applications
            foreach (ApplicationInfoViewModel applicationInfo in applications)
            {
                if (!resolved.Contains(applicationInfo.Guid))
                {
                    _resolvedApplications.Add(applicationInfo);
                }
                else
                {
                    // Find resolved application
                    var app = _resolvedApplications.First(x => x.Guid == applicationInfo.Guid);

                    // Update dynamic properties
                    app.Name = applicationInfo.Name;
                    app.API = applicationInfo.API;
                    app.DeviceUid = applicationInfo.DeviceUid;
                    app.DeviceObjects = applicationInfo.DeviceObjects;
                }
            }
            
            // Remove unregistered applications
            _resolvedApplications.RemoveMany(_resolvedApplications.Where(x => !incomingGuids.Contains(x.Guid)));
        }

        /// <summary>
        /// Internal connection
        /// </summary>
        private Workspace.ConnectionViewModel _connectionViewModel = new();

        /// <summary>
        /// Internal connection status
        /// </summary>
        private Models.Workspace.ConnectionStatus _connectionStatus;

        /// <summary>
        /// Internal selected application
        /// </summary>
        private ApplicationInfoViewModel? _selectedApplication;

        /// <summary>
        /// Internal application list
        /// </summary>
        private ObservableCollection<ApplicationInfoViewModel> _resolvedApplications = new();

        /// <summary>
        /// Internal discovery, unfiltered
        /// </summary>
        private List<ApplicationInfoViewModel> _discoveryApplications = new();

        /// <summary>
        /// Internal, default, connection string
        /// </summary>
        private string _connectionString = "ip:localhost";

        /// <summary>
        /// Internal connection state
        /// </summary>
        private bool _endpointConnected = false;

        /// <summary>
        /// Internal connection query
        /// </summary>
        private ConnectionQueryViewModel? _connectionQuery;

        /// <summary>
        /// Internal pending query, for endpoint checks
        /// </summary>
        private ConnectionQueryViewModel? _pendingQuery;

        /// <summary>
        /// Internal decorators
        /// </summary>
        private SourceList<QueryAttributeDecorator> _queryDecorators = new();

        /// <summary>
        /// Internal pooling timer
        /// </summary>
        private DispatcherTimer? _timer;
    }
}