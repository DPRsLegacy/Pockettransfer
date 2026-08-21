using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using PKHeX.Core;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Options;

namespace Pockettransfer.Server.Services;

public sealed class BankService(
    BankDbContext db,
    PkHexService pkhex,
    ConversionRules conversion,
    IOptions<BankOptions> options)
{
    public async Task<List<BankBox>> GetBoxesAsync(
        int userId,
        CancellationToken ct,
        Guid? sessionId = null,
        bool visibleOnly = true)
    {
        var boxes = await db.BankBoxes.AsNoTracking()
            .Where(b => b.UserId == userId)
            .Include(b => b.Pokemon)
            .OrderBy(b => b.Index)
            .ToListAsync(ct);
        if (!visibleOnly)
            return boxes;

        foreach (var box in boxes)
        {
            var visible = box.Pokemon.Where(p => VisibleInBank(p, sessionId)).ToList();
            box.Pokemon = visible;
        }

        return boxes;
    }

    public async Task<BankPokemon> DepositAsync(
        int userId,
        Guid sessionId,
        SaveFile sav,
        int box,
        int slot,
        int? destBoxIndex,
        int? destSlot,
        CancellationToken ct)
    {
        if (box < 0 || box >= sav.BoxCount || slot < 0 || slot >= sav.BoxSlotCount)
            throw new InvalidOperationException("Box or slot is out of range.");
        if (sav.IsBoxSlotLocked(box, slot) || sav.IsBoxSlotOverwriteProtected(box, slot))
            throw new InvalidOperationException("That save slot is locked.");

        var pk = sav.GetBoxSlotAtIndex(box, slot);
        if (pk.Species == 0)
            throw new InvalidOperationException("That box slot is empty.");

        var legality = pkhex.Analyze(pk, sav);
        if (!legality.Valid && !options.Value.AllowIllegalDeposit)
            throw new InvalidOperationException("PKHeX rejected this Pokémon as illegal.\n" + legality.Report);

        var destBox = await ResolveDestBoxAsync(userId, destBoxIndex, destSlot, ct);
        var slotIndex = destSlot ?? NextOpenSlot(destBox);
        if (destBox.Pokemon.Any(p => p.Slot == slotIndex))
            throw new InvalidOperationException("That bank slot is occupied.");

        var data = pkhex.ExportStored(pk);
        var entity = new BankPokemon
        {
            BankBoxId = destBox.Id,
            Slot = slotIndex,
            PkmData = data,
            Format = pk.Format,
            EntityContext = (int)pk.Context,
            Species = pk.Species,
            Form = pk.Form,
            Nickname = pk.Nickname,
            OriginalTrainer = pk.OriginalTrainerName,
            OriginVersion = pk.Version.ToString(),
            IsShiny = pk.IsShiny,
            Level = pk.CurrentLevel,
            IsLegal = legality.Valid,
            LegalityReport = legality.Report,
            Sha256 = PkHexService.Sha256Hex(data),
            DepositedAt = DateTimeOffset.UtcNow,
            Committed = false,
            HeldBySessionId = sessionId,
        };
        db.BankPokemon.Add(entity);
        sav.SetBoxSlotAtIndex(sav.BlankPKM, box, slot);
        await db.SaveChangesAsync(ct);
        return entity;
    }

    public async Task<BankPokemon> WithdrawAsync(
        int userId,
        Guid sessionId,
        SaveFile sav,
        int pokemonId,
        int box,
        int slot,
        CancellationToken ct)
    {
        if (box < 0 || box >= sav.BoxCount || slot < 0 || slot >= sav.BoxSlotCount)
            throw new InvalidOperationException("Box or slot is out of range.");
        if (sav.IsBoxSlotLocked(box, slot) || sav.IsBoxSlotOverwriteProtected(box, slot))
            throw new InvalidOperationException("That save slot is locked.");

        var occupant = sav.GetBoxSlotAtIndex(box, slot);
        if (occupant.Species != 0)
            throw new InvalidOperationException("Destination save slot is not empty.");

        var stored = await db.BankPokemon.Include(p => p.BankBox)
            .FirstOrDefaultAsync(p => p.Id == pokemonId && p.BankBox.UserId == userId, ct)
            ?? throw new InvalidOperationException("Pokémon not found in your bank.");

        if (stored.HeldBySessionId is Guid held && held != sessionId)
            throw new InvalidOperationException("That Pokémon is held by another save session.");
        if (stored.Committed && stored.HeldBySessionId is not null)
            throw new InvalidOperationException("That Pokémon is already being withdrawn.");

        var pk = pkhex.LoadPkm(stored.PkmData, (EntityContext)stored.EntityContext)
                 ?? throw new InvalidOperationException("Could not parse stored Pokémon data.");

        if (!conversion.CanTransferToSave(pk, sav, out var reason) && pk.GetType() != sav.PKMType)
            throw new InvalidOperationException(reason);

        var (converted, convertResult) = pkhex.ConvertToSave(pk, sav);
        if (converted is null)
            throw new InvalidOperationException($"Cannot convert to this save: {convertResult}.");

        var destLegality = pkhex.Analyze(converted, sav);
        if (!destLegality.Valid && !options.Value.AllowIllegalWithdraw)
            throw new InvalidOperationException("PKHeX rejected this Pokémon for the destination game.\n" + destLegality.Report);

        sav.SetBoxSlotAtIndex(converted, box, slot);
        if (!stored.Committed)
        {
            /* Undo a pending deposit: Pokémon was never committed to the bank. */
            db.BankPokemon.Remove(stored);
        }
        else
        {
            stored.HeldBySessionId = sessionId;
        }

        await db.SaveChangesAsync(ct);
        return stored;
    }

    public async Task CommitSessionAsync(int userId, Guid sessionId, CancellationToken ct)
    {
        var rows = await HeldForUserAsync(userId, sessionId, ct);
        foreach (var row in rows)
        {
            if (row.Committed)
                db.BankPokemon.Remove(row);
            else
            {
                row.Committed = true;
                row.HeldBySessionId = null;
            }
        }

        await db.SaveChangesAsync(ct);
    }

    public async Task AbandonSessionAsync(int userId, Guid sessionId, CancellationToken ct)
    {
        var rows = await HeldForUserAsync(userId, sessionId, ct);
        foreach (var row in rows)
        {
            if (row.Committed)
                row.HeldBySessionId = null;
            else
                db.BankPokemon.Remove(row);
        }

        await db.SaveChangesAsync(ct);
    }

    public async Task ReconcileOrphansAsync(IReadOnlySet<Guid> liveSessions, CancellationToken ct)
    {
        var held = await db.BankPokemon.Where(p => p.HeldBySessionId != null).ToListAsync(ct);
        var dirty = false;
        foreach (var row in held)
        {
            if (row.HeldBySessionId is not Guid sid || liveSessions.Contains(sid))
                continue;
            if (!row.Committed)
                row.Committed = true;
            row.HeldBySessionId = null;
            dirty = true;
        }

        if (dirty)
            await db.SaveChangesAsync(ct);
    }

    public SlotDto ToDto(BankPokemon p, PkHexService names, int boxIndex)
    {
        var sum = names.SummarizeStored(p.PkmData, p.EntityContext);
        return new(
            p.Id,
            p.BankBoxId,
            p.Slot,
            p.Species,
            names.SpeciesName((ushort)p.Species),
            p.Form,
            p.Nickname,
            p.OriginalTrainer,
            p.OriginVersion,
            p.IsShiny,
            p.Level,
            p.Format,
            p.IsLegal,
            p.LegalityReport,
            p.Sha256,
            p.DepositedAt,
            boxIndex,
            sum.Gender,
            sum.Tid,
            sum.Nature,
            sum.Type1,
            sum.Type2,
            sum.MetDate,
            sum.IvHp,
            sum.IvAtk,
            sum.IvDef,
            sum.IvSpa,
            sum.IvSpd,
            sum.IvSpe);
    }

    private async Task<List<BankPokemon>> HeldForUserAsync(int userId, Guid sessionId, CancellationToken ct) =>
        await db.BankPokemon.Include(p => p.BankBox)
            .Where(p => p.HeldBySessionId == sessionId && p.BankBox.UserId == userId)
            .ToListAsync(ct);

    private static bool VisibleInBank(BankPokemon p, Guid? sessionId)
    {
        if (p.Committed && p.HeldBySessionId is null)
            return true;
        if (!p.Committed && p.HeldBySessionId is Guid held)
            return sessionId is null || held == sessionId.Value;
        return false;
    }

    private async Task<BankBox> ResolveDestBoxAsync(int userId, int? destBoxIndex, int? destSlot, CancellationToken ct)
    {
        var boxes = await db.BankBoxes.Include(b => b.Pokemon)
            .Where(b => b.UserId == userId)
            .OrderBy(b => b.Index)
            .ToListAsync(ct);
        if (boxes.Count == 0)
            throw new InvalidOperationException("Bank boxes are not initialized.");

        if (destBoxIndex is int idx)
            return boxes.FirstOrDefault(b => b.Index == idx)
                   ?? throw new InvalidOperationException("Bank box not found.");

        foreach (var box in boxes)
        {
            if (box.Pokemon.Count < options.Value.SlotsPerBox)
                return box;
        }

        throw new InvalidOperationException("Bank is full.");
    }

    private int NextOpenSlot(BankBox box)
    {
        var used = box.Pokemon.Select(p => p.Slot).ToHashSet();
        for (var i = 0; i < options.Value.SlotsPerBox; i++)
        {
            if (!used.Contains(i))
                return i;
        }

        throw new InvalidOperationException("That bank box is full.");
    }
}

public sealed record SlotDto(
    int Id,
    int BoxId,
    int Slot,
    int Species,
    string SpeciesName,
    int Form,
    string Nickname,
    string OriginalTrainer,
    string OriginVersion,
    bool IsShiny,
    int Level,
    int Format,
    bool IsLegal,
    string LegalityReport,
    string Sha256,
    DateTimeOffset DepositedAt,
    int BoxIndex,
    int Gender = 2,
    int Tid = 0,
    string Nature = "",
    string Type1 = "",
    string Type2 = "",
    string MetDate = "",
    int IvHp = 0,
    int IvAtk = 0,
    int IvDef = 0,
    int IvSpa = 0,
    int IvSpd = 0,
    int IvSpe = 0);
