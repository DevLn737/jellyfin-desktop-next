// A media segment provider for the client's test stand (extras-server.sh):
// the segments of a video come from "<video name>.segments.json" next to it,
// [{"Type": "Intro", "Start": 0, "End": 10}, ...] in seconds. Never install
// it on a real server.
using System.Text.Json;
using Jellyfin.Database.Implementations.Enums;
using MediaBrowser.Common.Configuration;
using MediaBrowser.Common.Plugins;
using MediaBrowser.Controller;
using MediaBrowser.Controller.Entities;
using MediaBrowser.Controller.Library;
using MediaBrowser.Controller.MediaSegments;
using MediaBrowser.Controller.Plugins;
using MediaBrowser.Model;
using MediaBrowser.Model.MediaSegments;
using MediaBrowser.Model.Plugins;
using MediaBrowser.Model.Serialization;
using Microsoft.Extensions.DependencyInjection;

namespace JfdTestSegments;

// The configuration-backed base class sets the plugin's version and paths.
public class Plugin(IApplicationPaths applicationPaths, IXmlSerializer xmlSerializer)
    : BasePlugin<BasePluginConfiguration>(applicationPaths, xmlSerializer)
{
    public override string Name => "JFD Test Segments";

    public override Guid Id => Guid.Parse("5c3b6c1e-7f0a-4d1e-9a51-0f2a6c4b9d17");
}

public class Registrator : IPluginServiceRegistrator
{
    public void RegisterServices(IServiceCollection serviceCollection, IServerApplicationHost applicationHost)
        => serviceCollection.AddSingleton<IMediaSegmentProvider, SidecarSegmentProvider>();
}

public class SidecarSegmentProvider(ILibraryManager libraryManager) : IMediaSegmentProvider
{
    private sealed record Entry(string Type, double Start, double End);

    public string Name => "JFD Test Segments";

    private static string? SidecarOf(BaseItem item)
    {
        if (string.IsNullOrEmpty(item.Path))
        {
            return null;
        }

        var sidecar = Path.ChangeExtension(item.Path, ".segments.json");
        return File.Exists(sidecar) ? sidecar : null;
    }

    public ValueTask<bool> Supports(BaseItem item) => ValueTask.FromResult(item is Video && SidecarOf(item) is not null);

    public async Task<IReadOnlyList<MediaSegmentDto>> GetMediaSegments(MediaSegmentGenerationRequest request, CancellationToken cancellationToken)
    {
        var item = libraryManager.GetItemById(request.ItemId);
        var sidecar = item is null ? null : SidecarOf(item);
        if (sidecar is null)
        {
            return [];
        }

        await using var stream = File.OpenRead(sidecar);
        var entries = await JsonSerializer.DeserializeAsync<List<Entry>>(stream, cancellationToken: cancellationToken).ConfigureAwait(false) ?? [];
        return entries.Select(e => new MediaSegmentDto
        {
            ItemId = request.ItemId,
            Type = Enum.Parse<MediaSegmentType>(e.Type),
            StartTicks = TimeSpan.FromSeconds(e.Start).Ticks,
            EndTicks = TimeSpan.FromSeconds(e.End).Ticks,
        }).ToList();
    }

    public Task CleanupExtractedData(Guid itemId, CancellationToken cancellationToken) => Task.CompletedTask;
}
