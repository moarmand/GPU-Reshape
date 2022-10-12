﻿using System;

namespace Runtime.Models.Objects
{
    public struct ShaderIdentifier
    {
        /// <summary>
        /// Global UID
        /// </summary>
        public UInt64 GUID { get; set; }

        /// <summary>
        /// Readable descriptor
        /// </summary>
        public string Descriptor { get; set; }
    }
}