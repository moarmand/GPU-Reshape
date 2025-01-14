// 
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
using System.Diagnostics;
using System.Reactive.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using ReactiveUI;
using Runtime.ViewModels.Objects;
using Studio.Extensions;
using Studio.Models.Logging;
using Studio.Services;
using Studio.ViewModels.Tools;
using Studio.ViewModels.Workspace.Objects;

namespace Studio.Views.Tools
{
    public partial class ShaderTreeView : UserControl
    {
        public ShaderTreeView()
        {
            InitializeComponent();

            this.WhenAnyValue(x => x.DataContext)
                .WhereNotNull()
                .Subscribe(x =>
                {
                    // Bind signals
                    ShaderList.Events().DoubleTapped
                        .Select(_ => ShaderList.SelectedItem)
                        .WhereNotNull()
                        .InvokeCommand(this, self => self.VM!.OpenShaderDocument);
                });
        }

        private void OnShaderIdentifierLayoutUpdated(object? sender, EventArgs e)
        {
            // Expected sender?
            if (sender is not Control { DataContext: ShaderIdentifierViewModel shaderIdentifierViewModel })
                return;

            // May already be pooled
            if (shaderIdentifierViewModel.HasBeenPooled)
                return;
            
            // Enqueue request
            VM?.PopulateSingle(shaderIdentifierViewModel);

            // Mark as pooled
            shaderIdentifierViewModel.HasBeenPooled = true;
        }
        
        private ShaderTreeViewModel? VM => DataContext as ShaderTreeViewModel;
    }
}
