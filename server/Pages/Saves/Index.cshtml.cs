using System.Security.Claims;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.Extensions.Options;
using Pockettransfer.Server.Options;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Saves;

public class IndexModel(PkHexService pkhex, SaveSessionStore sessions, IOptions<BankOptions> options) : PageModel
{
    public string? Error { get; private set; }
    public Guid? SessionId { get; private set; }
    public string? Summary { get; private set; }
    public IReadOnlyList<SaveSlotView> Slots { get; private set; } = [];
    public int Box { get; private set; }
    public int BoxCount { get; private set; }

    public void OnGet(Guid? sessionId, int box = 0)
    {
        if (sessionId is Guid id)
            Load(id, box);
    }

    public async Task<IActionResult> OnPostAsync(IFormFile? file, CancellationToken ct)
    {
        if (file is null || file.Length == 0)
        {
            Error = "Choose a save file.";
            return Page();
        }

        if (file.Length > options.Value.MaxSaveBytes)
        {
            Error = "Save is too large.";
            return Page();
        }

        await using var ms = new MemoryStream();
        await file.CopyToAsync(ms, ct);
        var sav = pkhex.LoadSave(ms.ToArray(), file.FileName);
        if (sav is null)
        {
            Error = "PKHeX could not read this save.";
            return Page();
        }

        var userId = int.Parse(User.FindFirstValue(ClaimTypes.NameIdentifier)!);
        var session = sessions.Create(userId, sav, file.FileName, TimeSpan.FromMinutes(options.Value.SessionMinutes));
        return RedirectToPage(new { sessionId = session.Id, box = 0 });
    }

    private void Load(Guid id, int box)
    {
        var userId = int.Parse(User.FindFirstValue(ClaimTypes.NameIdentifier)!);
        var session = sessions.Get(id, userId);
        if (session is null)
        {
            Error = "Save session expired. Upload again.";
            return;
        }

        SessionId = id;
        var sav = session.Save;
        BoxCount = sav.BoxCount;
        Box = Math.Clamp(box, 0, Math.Max(0, sav.BoxCount - 1));
        Summary = $"{sav.Version} · {sav.OT} · TID {sav.DisplayTID} · checksums {(sav.ChecksumsValid ? "ok" : "INVALID")}";
        var list = new List<SaveSlotView>();
        for (var s = 0; s < sav.BoxSlotCount; s++)
        {
            var pk = sav.GetBoxSlotAtIndex(Box, s);
            if (pk.Species == 0)
            {
                list.Add(new SaveSlotView(s, true, "", false, false, 0));
                continue;
            }

            var legal = pkhex.Analyze(pk, sav).Valid;
            list.Add(new SaveSlotView(s, false, pk.Nickname, pk.IsShiny, legal, pk.CurrentLevel));
        }

        Slots = list;
    }

    public sealed record SaveSlotView(int Slot, bool Empty, string Nickname, bool Shiny, bool Legal, int Level);
}
