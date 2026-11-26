using System;
using MediaBrowser.Common.Configuration;
using MediaBrowser.Common.Plugins;
using MediaBrowser.Model.Plugins;
using MediaBrowser.Model.Serialization;

namespace Jellyfin.Plugin.RoomMembers;

/// <summary>
/// Lets the members of a SyncPlay group see where each of them is in the
/// group's item and how far each is from the server (GET /RoomMembers).
/// Stock Jellyfin tells members only each other's user names.
/// </summary>
public class Plugin : BasePlugin<BasePluginConfiguration>
{
    /// <summary>The plugin's fixed id.</summary>
    public static readonly Guid PluginId = new("75234109-bd2e-4b2b-9ee5-3c0943aac752");

    /// <summary>Initializes a new instance of the <see cref="Plugin"/> class.</summary>
    /// <param name="applicationPaths">The server's paths.</param>
    /// <param name="xmlSerializer">The server's XML serializer.</param>
    public Plugin(IApplicationPaths applicationPaths, IXmlSerializer xmlSerializer)
        : base(applicationPaths, xmlSerializer)
    {
    }

    /// <inheritdoc />
    public override string Name => "Room Members";

    /// <inheritdoc />
    public override Guid Id => PluginId;

    /// <inheritdoc />
    public override string Description =>
        "Shows SyncPlay group members each other's playback position and ping (used by Jellyfin Desktop Next).";
}
