using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Admin;

public class PokemonModel(BankDbContext db, PkHexService pkhex, AdminService admin) : PageModel
{
    public SlotDto? Pokemon { get; private set; }
    public int OwnerId { get; private set; }
    public string OwnerName { get; private set; } = "";

    [TempData]
    public string? Status { get; set; }

    public string? Error { get; set; }

    public async Task<IActionResult> OnGetAsync(int id, CancellationToken ct)
    {
        return await LoadAsync(id, ct);
    }

    public async Task<IActionResult> OnGetDownloadAsync(int id, CancellationToken ct)
    {
        var row = await db.BankPokemon.AsNoTracking()
            .Include(p => p.BankBox).ThenInclude(b => b.User)
            .FirstOrDefaultAsync(p => p.Id == id, ct);
        if (row is null)
            return NotFound();

        var ext = row.Format is >= 1 and <= 9 ? $"pk{row.Format}" : "bin";
        var name = $"{Sanitize(row.Nickname)}.{ext}";
        return File(row.PkmData, "application/octet-stream", name);
    }

    public async Task<IActionResult> OnPostDeleteAsync(int id, CancellationToken ct)
    {
        var row = await db.BankPokemon.Include(p => p.BankBox).ThenInclude(b => b.User)
            .FirstOrDefaultAsync(p => p.Id == id, ct);
        if (row is null)
            return NotFound();

        var ownerId = row.BankBox.UserId;
        var boxIndex = row.BankBox.Index;
        try
        {
            await admin.DeletePokemonAsync(id, ct);
            TempData["Status"] = $"Removed {row.Nickname} from the bank.";
            return RedirectToPage("/Admin/Users/Bank", new { id = ownerId, box = boxIndex });
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            return await LoadAsync(id, ct);
        }
    }

    private async Task<IActionResult> LoadAsync(int id, CancellationToken ct)
    {
        var row = await db.BankPokemon.AsNoTracking()
            .Include(p => p.BankBox).ThenInclude(b => b.User)
            .FirstOrDefaultAsync(p => p.Id == id, ct);
        if (row is null)
            return NotFound();

        OwnerId = row.BankBox.UserId;
        OwnerName = row.BankBox.User.Username;
        Pokemon = new SlotDto(
            row.Id, row.BankBoxId, row.Slot, row.Species, pkhex.SpeciesName((ushort)row.Species),
            row.Form, row.Nickname, row.OriginalTrainer, row.OriginVersion, row.IsShiny, row.Level,
            row.Format, row.IsLegal, row.LegalityReport, row.Sha256, row.DepositedAt,
            row.BankBox.Index);
        return Page();
    }

    private static string Sanitize(string name)
    {
        var cleaned = new string(name.Where(c => char.IsLetterOrDigit(c) || c is '-' or '_').ToArray());
        return cleaned.Length == 0 ? "pokemon" : cleaned;
    }
}
