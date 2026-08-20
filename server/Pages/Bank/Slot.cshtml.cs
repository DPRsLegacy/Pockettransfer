using System.Security.Claims;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Bank;

public class SlotModel(BankDbContext db, PkHexService pkhex) : PageModel
{
    public SlotDto? Pokemon { get; private set; }

    public async Task<IActionResult> OnGetAsync(int id, CancellationToken ct)
    {
        var userId = int.Parse(User.FindFirstValue(ClaimTypes.NameIdentifier)!);
        var row = await db.BankPokemon.Include(p => p.BankBox)
            .FirstOrDefaultAsync(p => p.Id == id && p.BankBox.UserId == userId, ct);
        if (row is null)
            return NotFound();
        Pokemon = new SlotDto(
            row.Id, row.BankBoxId, row.Slot, row.Species, pkhex.SpeciesName((ushort)row.Species),
            row.Form, row.Nickname, row.OriginalTrainer, row.OriginVersion, row.IsShiny, row.Level,
            row.Format, row.IsLegal, row.LegalityReport, row.Sha256, row.DepositedAt,
            row.BankBox.Index);
        return Page();
    }
}
