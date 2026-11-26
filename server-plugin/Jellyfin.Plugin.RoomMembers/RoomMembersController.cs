using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using MediaBrowser.Controller.Session;
using MediaBrowser.Controller.SyncPlay;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;

namespace Jellyfin.Plugin.RoomMembers;

/// <summary>
/// GET /RoomMembers: the members of the caller's SyncPlay group with their
/// last reported position and SyncPlay ping. Only a member of a group sees
/// that group; anyone else gets an empty answer.
/// </summary>
[ApiController]
[Authorize]
[Route("RoomMembers")]
public class RoomMembersController : ControllerBase
{
    // Claim types the server's authentication handler sets (Jellyfin.Api
    // InternalClaimTypes, not referenced by plugins).
    private const string UserIdClaim = "Jellyfin-UserId";
    private const string DeviceIdClaim = "Jellyfin-DeviceId";

    private readonly ISessionManager _sessionManager;
    private readonly ISyncPlayManager _syncPlayManager;
    private readonly ILogger<RoomMembersController> _logger;

    /// <summary>Initializes a new instance of the <see cref="RoomMembersController"/> class.</summary>
    /// <param name="sessionManager">The session manager.</param>
    /// <param name="syncPlayManager">The SyncPlay manager.</param>
    /// <param name="logger">The logger.</param>
    public RoomMembersController(
        ISessionManager sessionManager,
        ISyncPlayManager syncPlayManager,
        ILogger<RoomMembersController> logger)
    {
        _sessionManager = sessionManager;
        _syncPlayManager = syncPlayManager;
        _logger = logger;
    }

    /// <summary>Gets the members of the caller's group.</summary>
    /// <returns>The group and its members; no group when the caller is in none.</returns>
    [HttpGet]
    [ProducesResponseType(StatusCodes.Status200OK)]
    public ActionResult<RoomMembersDto> Get()
    {
        var answer = new RoomMembersDto { ServerTimeUtc = DateTime.UtcNow };
        var self = FindCallerSession();
        if (self is null)
        {
            return answer;
        }

        var group = GroupReader.GroupOf(_syncPlayManager, self.Id, _logger);
        if (group is null)
        {
            return answer;
        }

        answer.GroupId = group.Value.GroupId;
        var sessions = _sessionManager.Sessions.ToDictionary(s => s.Id, StringComparer.OrdinalIgnoreCase);
        foreach (var member in group.Value.Members)
        {
            sessions.TryGetValue(member.SessionId, out var session);
            var state = session?.PlayState;
            answer.Members.Add(new RoomMemberDto
            {
                UserName = member.UserName,
                DeviceName = session?.DeviceName ?? string.Empty,
                Client = session?.Client ?? string.Empty,
                IsSelf = string.Equals(member.SessionId, self.Id, StringComparison.OrdinalIgnoreCase),
                PingMs = member.Ping,
                IsBuffering = member.IsBuffering,
                ItemId = session?.NowPlayingItem?.Id,
                PositionTicks = session?.NowPlayingItem is null ? null : state?.PositionTicks,
                IsPaused = state?.IsPaused ?? true,
                PositionUpdatedUtc = session?.NowPlayingItem is null ? null : session.LastPlaybackCheckIn,
            });
        }

        return answer;
    }

    private SessionInfo? FindCallerSession()
    {
        var deviceId = User.FindFirst(DeviceIdClaim)?.Value;
        if (string.IsNullOrEmpty(deviceId) || !Guid.TryParse(User.FindFirst(UserIdClaim)?.Value, out var userId))
        {
            return null;
        }

        return _sessionManager.Sessions.FirstOrDefault(s =>
            s.UserId.Equals(userId) && string.Equals(s.DeviceId, deviceId, StringComparison.Ordinal));
    }
}

/// <summary>Reads the server's SyncPlay groups, which it does not expose to plugins.</summary>
internal static class GroupReader
{
    internal readonly record struct GroupView(Guid GroupId, IReadOnlyList<GroupMember> Members);

    private static bool _warned;

    /// <summary>The group the session is in, with a copy of its members.</summary>
    internal static GroupView? GroupOf(ISyncPlayManager manager, string sessionId, ILogger logger)
    {
        try
        {
            // SyncPlayManager._sessionToGroupMap: session id -> Group.
            var mapField = manager.GetType().GetField("_sessionToGroupMap", BindingFlags.Instance | BindingFlags.NonPublic);
            if (mapField?.GetValue(manager) is not IDictionary map || !map.Contains(sessionId))
            {
                Warn(mapField is null, logger, "_sessionToGroupMap");
                return null;
            }

            var group = map[sessionId];
            if (group is null)
            {
                return null;
            }

            var type = group.GetType();
            var groupId = type.GetProperty("GroupId")?.GetValue(group) is Guid id ? id : Guid.Empty;
            // Group._participants: session id -> GroupMember. The server
            // changes it only while holding the group's lock.
            var participantsField = type.GetField("_participants", BindingFlags.Instance | BindingFlags.NonPublic);
            if (participantsField?.GetValue(group) is not IDictionary participants)
            {
                Warn(true, logger, "_participants");
                return null;
            }

            List<GroupMember> members;
            lock (group)
            {
                members = participants.Values.OfType<GroupMember>().ToList();
            }

            return new GroupView(groupId, members);
        }
        catch (Exception e) when (e is TargetException or FieldAccessException or InvalidCastException)
        {
            logger.LogWarning(e, "Cannot read SyncPlay groups");
            return null;
        }
    }

    private static void Warn(bool missing, ILogger logger, string field)
    {
        if (!missing || _warned)
        {
            return;
        }

        _warned = true;
        logger.LogWarning("This Jellyfin version has no SyncPlay field {Field}; room members cannot be shown", field);
    }
}

/// <summary>The caller's group and its members.</summary>
public class RoomMembersDto
{
    /// <summary>Gets or sets the group id; null when the caller is in no group.</summary>
    public Guid? GroupId { get; set; }

    /// <summary>Gets or sets the server's time when the answer was made.</summary>
    public DateTime ServerTimeUtc { get; set; }

    /// <summary>Gets the members, one per session.</summary>
    public List<RoomMemberDto> Members { get; } = new();
}

/// <summary>One member (session) of a group.</summary>
public class RoomMemberDto
{
    /// <summary>Gets or sets the user name.</summary>
    public string UserName { get; set; } = string.Empty;

    /// <summary>Gets or sets the device name.</summary>
    public string DeviceName { get; set; } = string.Empty;

    /// <summary>Gets or sets the client application.</summary>
    public string Client { get; set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether this is the caller's own session.</summary>
    public bool IsSelf { get; set; }

    /// <summary>Gets or sets the ping the member last reported to the group, in milliseconds.</summary>
    public long PingMs { get; set; }

    /// <summary>Gets or sets a value indicating whether the group waits for this member.</summary>
    public bool IsBuffering { get; set; }

    /// <summary>Gets or sets the item the member plays, if any.</summary>
    public Guid? ItemId { get; set; }

    /// <summary>Gets or sets the member's last reported position.</summary>
    public long? PositionTicks { get; set; }

    /// <summary>Gets or sets a value indicating whether the member was paused at that report.</summary>
    public bool IsPaused { get; set; }

    /// <summary>Gets or sets when the position was reported (server time, UTC).</summary>
    public DateTime? PositionUpdatedUtc { get; set; }
}
