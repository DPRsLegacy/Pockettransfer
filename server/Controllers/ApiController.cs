using System.Security.Claims;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Options;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Controllers;

[ApiController]
[Route("api")]
public sealed class ApiController(
    AccountService accounts,
    BankService bank,
    PkHexService pkhex,
    ConversionRules conversion,
    SaveSessionStore sessions,
    GameCatalog games,
    BankDbContext db,
    IOptions<BankOptions> options) : ControllerBase
{
    [AllowAnonymous]
    [HttpPost("auth/register")]
    public async Task<ActionResult> Register([FromBody] AuthRequest body, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(body.Username) || string.IsNullOrWhiteSpace(body.Password))
            return BadRequest(new { error = "Username and password are required." });
        try
        {
            var user = await accounts.RegisterAsync(body.Username, body.Password, ct);
            var token = await accounts.IssueDeviceTokenAsync(
                user, body.Name ?? "console", body.Platform ?? "api", ct);
            return Ok(new { token, userId = user.Id, username = user.Username });
        }
        catch (InvalidOperationException ex)
        {
            return Conflict(new { error = ex.Message });
        }
    }

    [AllowAnonymous]
    [EnableRateLimiting("login")]
    [HttpPost("auth/login")]
    public async Task<ActionResult> Login([FromBody] AuthRequest body, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(body.Username) || string.IsNullOrWhiteSpace(body.Password))
            return BadRequest(new { error = "Username and password are required." });
        var result = await accounts.AuthenticateAsync(
            body.Username, body.Password, LoginThrottle.ClientKey(HttpContext), ct);
        if (result.RateLimited)
            return StatusCode(StatusCodes.Status429TooManyRequests, new { error = LoginThrottle.TooManyAttempts });
        if (result.User is null)
            return Unauthorized(new { error = "Invalid username or password." });
        await accounts.EnsureBoxesAsync(result.User.Id, ct);
        var token = await accounts.IssueDeviceTokenAsync(
            result.User, body.Name ?? "api-login", body.Platform ?? "api", ct);
        return Ok(new { token, username = result.User.Username, userId = result.User.Id });
    }

    [Authorize(Policy = "AnyUser")]
    [HttpPost("auth/devices")]
    public async Task<ActionResult> CreatePairingCode(CancellationToken ct)
    {
        var code = await accounts.CreatePairingCodeAsync(CurrentUserId(), ct);
        return Ok(new { code, expiresMinutes = options.Value.PairingMinutes });
    }

    [AllowAnonymous]
    [HttpPost("auth/devices/pair")]
    public async Task<ActionResult> Pair([FromBody] PairRequest body, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(body.Code))
            return BadRequest(new { error = "Pairing code is required." });
        var result = await accounts.PairDeviceAsync(body.Code, body.Name ?? "console", body.Platform ?? "unknown", ct);
        if (result is null)
            return Unauthorized(new { error = "Invalid or expired pairing code." });
        return Ok(new { token = result.Value.Token, email = result.Value.User.Email });
    }

    [AllowAnonymous]
    [HttpPost("auth/devices/enroll")]
    public async Task<ActionResult> Enroll([FromBody] EnrollRequest? body, CancellationToken ct)
    {
        var result = await accounts.EnrollConsoleAsync(
            body?.Name ?? "console",
            body?.Platform ?? "unknown",
            ct);
        if (result is null)
        {
            return Conflict(new
            {
                error = "No pairing waiting. Log in on the website, open Devices, generate a pairing code, then try again.",
            });
        }

        return Ok(new { token = result.Value.Token, username = result.Value.User.Username });
    }

    [Authorize(Policy = "AnyUser")]
    [HttpGet("games")]
    public ActionResult GetGames() => Ok(games.Games);

    [Authorize(Policy = "AnyUser")]
    [HttpGet("bank/boxes")]
    public async Task<ActionResult> GetBank([FromQuery] Guid? sessionId, CancellationToken ct)
    {
        await bank.ReconcileOrphansAsync(sessions.ActiveIds(), ct);
        var boxes = await bank.GetBoxesAsync(CurrentUserId(), ct, sessionId);
        return Ok(boxes.Select(b => new
        {
            b.Id,
            b.Index,
            b.Name,
            slots = b.Pokemon.OrderBy(p => p.Slot).Select(p => bank.ToDto(p, pkhex, b.Index)),
        }));
    }

    [Authorize(Policy = "AnyUser")]
    [HttpPost("saves/session")]
    [RequestSizeLimit(10_000_000)]
    public async Task<ActionResult> OpenSession(CancellationToken ct)
    {
        var file = Request.Form.Files.GetFile("file") ?? Request.Form.Files.FirstOrDefault();
        if (file is null)
            return BadRequest(new { error = "Upload a save as form field 'file'." });
        if (file.Length > options.Value.MaxSaveBytes)
            return BadRequest(new { error = "Save is too large." });

        await using var ms = new MemoryStream();
        await file.CopyToAsync(ms, ct);
        var sav = pkhex.LoadSave(ms.ToArray(), file.FileName);
        if (sav is null)
            return BadRequest(new { error = "PKHeX could not read this save file." });

        await bank.ReconcileOrphansAsync(sessions.ActiveIds(), ct);
        var session = sessions.Create(CurrentUserId(), sav, file.FileName, TimeSpan.FromMinutes(options.Value.SessionMinutes));
        return Ok(SessionSummary(session));
    }

    [Authorize(Policy = "AnyUser")]
    [HttpGet("saves/{id:guid}")]
    public ActionResult GetSession(Guid id)
    {
        var session = sessions.Get(id, CurrentUserId());
        return session is null ? NotFound(new { error = "Save session expired or missing." }) : Ok(SessionSummary(session));
    }

    [Authorize(Policy = "AnyUser")]
    [HttpGet("saves/{id:guid}/boxes")]
    public ActionResult GetSaveBoxes(Guid id, [FromQuery] int box = 0)
    {
        var session = sessions.Get(id, CurrentUserId());
        if (session is null)
            return NotFound(new { error = "Save session expired or missing." });
        var sav = session.Save;
        if (box < 0 || box >= sav.BoxCount)
            return BadRequest(new { error = "Box out of range." });

        var slots = new List<object>(sav.BoxSlotCount);
        for (var s = 0; s < sav.BoxSlotCount; s++)
        {
            var pk = sav.GetBoxSlotAtIndex(box, s);
            if (pk.Species == 0)
            {
                slots.Add(new { slot = s, empty = true });
                continue;
            }

            var legality = pkhex.Analyze(pk, sav);
            slots.Add(new
            {
                slot = s,
                empty = false,
                species = (int)pk.Species,
                speciesName = pkhex.SpeciesName(pk.Species),
                pk.Form,
                pk.Nickname,
                originalTrainer = pk.OriginalTrainerName,
                pk.IsShiny,
                level = pk.CurrentLevel,
                legal = legality.Valid,
                locked = sav.IsBoxSlotLocked(box, s),
            });
        }

        return Ok(new { box, boxCount = sav.BoxCount, slotCount = sav.BoxSlotCount, slots });
    }

    [Authorize(Policy = "AnyUser")]
    [HttpGet("saves/{id:guid}/file")]
    public ActionResult DownloadSave(Guid id)
    {
        var session = sessions.Get(id, CurrentUserId());
        if (session is null)
            return NotFound(new { error = "Save session expired or missing." });
        var bytes = pkhex.ExportSave(session.Save);
        return File(bytes, "application/octet-stream", session.FileName);
    }

    [Authorize(Policy = "AnyUser")]
    [HttpPost("saves/{id:guid}/commit")]
    public async Task<ActionResult> CommitSession(Guid id, CancellationToken ct)
    {
        try
        {
            await bank.CommitSessionAsync(CurrentUserId(), id, ct);
            sessions.Remove(id);
            return Ok(new { ok = true, sessionId = id });
        }
        catch (InvalidOperationException ex)
        {
            return BadRequest(new { error = ex.Message });
        }
    }

    [Authorize(Policy = "AnyUser")]
    [HttpDelete("saves/{id:guid}")]
    public async Task<ActionResult> CloseSession(Guid id, CancellationToken ct)
    {
        await bank.AbandonSessionAsync(CurrentUserId(), id, ct);
        sessions.Remove(id);
        return NoContent();
    }

    [Authorize(Policy = "AnyUser")]
    [HttpPost("bank/deposit")]
    public async Task<ActionResult> Deposit([FromBody] DepositRequest body, CancellationToken ct)
    {
        var session = sessions.Get(body.SessionId, CurrentUserId());
        if (session is null)
            return NotFound(new { error = "Save session expired or missing." });
        try
        {
            var stored = await bank.DepositAsync(
                CurrentUserId(), session.Id, session.Save, body.Box, body.Slot, body.BankBox, body.BankSlot, ct);
            sessions.Touch(session, TimeSpan.FromMinutes(options.Value.SessionMinutes));
            var destBox = await db.BankBoxes.AsNoTracking().FirstAsync(b => b.Id == stored.BankBoxId, ct);
            return Ok(bank.ToDto(stored, pkhex, destBox.Index));
        }
        catch (InvalidOperationException ex)
        {
            return BadRequest(new { error = ex.Message });
        }
    }

    [Authorize(Policy = "AnyUser")]
    [HttpPost("bank/withdraw")]
    public async Task<ActionResult> Withdraw([FromBody] WithdrawRequest body, CancellationToken ct)
    {
        var session = sessions.Get(body.SessionId, CurrentUserId());
        if (session is null)
            return NotFound(new { error = "Save session expired or missing." });
        try
        {
            await bank.WithdrawAsync(CurrentUserId(), session.Id, session.Save, body.PokemonId, body.Box, body.Slot, ct);
            sessions.Touch(session, TimeSpan.FromMinutes(options.Value.SessionMinutes));
            return Ok(new { ok = true, sessionId = session.Id });
        }
        catch (InvalidOperationException ex)
        {
            return BadRequest(new { error = ex.Message });
        }
    }

    [Authorize(Policy = "AnyUser")]
    [HttpPost("legality")]
    public ActionResult Legality([FromBody] LegalityRequest body)
    {
        PKHeX.Core.PKM? pk = null;
        PKHeX.Core.SaveFile? sav = null;
        if (body.SessionId is { } sid && body.Box is { } box && body.Slot is { } slot)
        {
            var session = sessions.Get(sid, CurrentUserId());
            if (session is null)
                return NotFound(new { error = "Save session expired or missing." });
            sav = session.Save;
            pk = sav.GetBoxSlotAtIndex(box, slot);
        }
        else if (!string.IsNullOrEmpty(body.PkmBase64))
        {
            pk = pkhex.LoadPkmBySize(Convert.FromBase64String(body.PkmBase64));
        }

        if (pk is null || pk.Species == 0)
            return BadRequest(new { error = "No Pokémon to analyze." });

        var report = pkhex.Analyze(pk, sav);
        string? transfer = null;
        if (sav is not null)
            conversion.CanTransferToSave(pk, sav, out transfer);

        return Ok(new { report.Valid, report.Report, report.Issues, transfer });
    }

    [Authorize(Policy = "AnyUser")]
    [HttpGet("auth/devices")]
    public async Task<ActionResult> ListDevices(CancellationToken ct)
    {
        var userId = CurrentUserId();
        var devices = await db.Devices.AsNoTracking()
            .Where(d => d.UserId == userId)
            .OrderByDescending(d => d.CreatedAt)
            .Select(d => new { d.Id, d.Name, d.Platform, d.CreatedAt, d.LastUsedAt })
            .ToListAsync(ct);
        return Ok(devices);
    }

    private int CurrentUserId()
    {
        var value = User.FindFirstValue(ClaimTypes.NameIdentifier);
        return int.Parse(value!);
    }

    private static object SessionSummary(SaveSession session)
    {
        var sav = session.Save;
        return new
        {
            sessionId = session.Id,
            fileName = session.FileName,
            game = sav.Version.ToString(),
            generation = sav.Generation,
            trainer = sav.OT,
            tid = sav.DisplayTID,
            boxCount = sav.BoxCount,
            boxSlotCount = sav.BoxSlotCount,
            checksumsValid = sav.ChecksumsValid,
            expiresAt = session.ExpiresAt,
        };
    }
}

public sealed record AuthRequest(string Username, string Password, string? Name = null, string? Platform = null);
public sealed record PairRequest(string Code, string? Name, string? Platform);
public sealed record EnrollRequest(string? Name, string? Platform);
public sealed record DepositRequest(Guid SessionId, int Box, int Slot, int? BankBox, int? BankSlot);
public sealed record WithdrawRequest(Guid SessionId, int PokemonId, int Box, int Slot);
public sealed record LegalityRequest(Guid? SessionId, int? Box, int? Slot, string? PkmBase64);
