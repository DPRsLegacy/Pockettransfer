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
    public async Task<List<BankBox>> GetBoxesAsync(int userId, CancellationToken ct) =>
        await db.BankBoxes.AsNoTracking()
            .Where(b => b.UserId == userId)
            .Include(b => b.Pokemon)
            .OrderBy(b => b.Index)
            .ToListAsync(ct);

    public async Task<BankPokemon> DepositAsync(
        int userId,
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
        };
        db.BankPokemon.Add(entity);
        sav.SetBoxSlotAtIndex(sav.BlankPKM, box, slot);
        await db.SaveChangesAsync(ct);
        return entity;
    }

    public async Task<BankPokemon> WithdrawAsync(
        int userId,
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
        db.BankPokemon.Remove(stored);
        await db.SaveChangesAsync(ct);
        return stored;
    }

    public SlotDto ToDto(BankPokemon p, PkHexService names, int boxIndex) => new(
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
        boxIndex);

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
    int BoxIndex);
