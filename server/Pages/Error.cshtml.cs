using System.Diagnostics;
using Microsoft.AspNetCore.Diagnostics;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace Pockettransfer.Server.Pages;

[ResponseCache(Duration = 0, Location = ResponseCacheLocation.None, NoStore = true)]
[IgnoreAntiforgeryToken]
public class ErrorModel(ILogger<ErrorModel> logger, IWebHostEnvironment env) : PageModel
{
    public string? RequestId { get; set; }
    public string? ExceptionMessage { get; set; }
    public bool ShowDetails { get; private set; }

    public bool ShowRequestId => !string.IsNullOrEmpty(RequestId);

    public void OnGet()
    {
        RequestId = Activity.Current?.Id ?? HttpContext.TraceIdentifier;
        ShowDetails = env.IsDevelopment()
            || string.Equals(Environment.GetEnvironmentVariable("PT_DETAILED_ERRORS"), "true", StringComparison.OrdinalIgnoreCase);

        var feature = HttpContext.Features.Get<IExceptionHandlerPathFeature>();
        if (feature?.Error is Exception ex)
        {
            logger.LogError(ex, "Unhandled exception for {Path}", feature.Path);
            if (ShowDetails)
                ExceptionMessage = ex.ToString();
        }
    }
}
