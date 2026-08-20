using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.AspNetCore.Http.Features;
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.AspNetCore.Identity;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using Pockettransfer.Server.Auth;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Options;
using Pockettransfer.Server.Services;

var builder = WebApplication.CreateBuilder(args);

builder.Services.Configure<BankOptions>(builder.Configuration.GetSection(BankOptions.SectionName));
builder.Services.Configure<FormOptions>(o => o.MultipartBodyLengthLimit = 10_000_000);
builder.Services.Configure<ForwardedHeadersOptions>(o =>
{
    o.ForwardedHeaders = ForwardedHeaders.XForwardedFor | ForwardedHeaders.XForwardedProto | ForwardedHeaders.XForwardedHost;
    o.KnownIPNetworks.Clear();
    o.KnownProxies.Clear();
});

var dataDir = Environment.GetEnvironmentVariable("PT_DATA_DIR")
    ?? Path.Combine(builder.Environment.ContentRootPath, "Data");
Directory.CreateDirectory(dataDir);
builder.Services.AddDataProtection()
    .PersistKeysToFileSystem(new DirectoryInfo(Path.Combine(dataDir, "keys")));

var connection = builder.Configuration.GetConnectionString("Bank")
    ?? Environment.GetEnvironmentVariable("ConnectionStrings__Bank")
    ?? "Data Source=Data/pockettransfer.db";

builder.Services.AddDbContext<BankDbContext>(o =>
{
    if (IsPostgres(connection))
        o.UseNpgsql(connection);
    else
    {
        Directory.CreateDirectory(Path.Combine(builder.Environment.ContentRootPath, "Data"));
        o.UseSqlite(connection);
    }
});

builder.Services.AddSingleton<IPasswordHasher<User>, PasswordHasher<User>>();
builder.Services.AddSingleton<SaveSessionStore>();
builder.Services.AddSingleton<GameCatalog>();
builder.Services.AddScoped<AccountService>();
builder.Services.AddScoped<AdminService>();
builder.Services.AddScoped<PkHexService>();
builder.Services.AddScoped<ConversionRules>();
builder.Services.AddScoped<BankService>();
builder.Services.AddScoped<IAuthorizationHandler, AdminAuthorizationHandler>();

var useSecureCookies = !builder.Environment.IsDevelopment();
builder.Services.AddAuthentication(CookieAuthenticationDefaults.AuthenticationScheme)
    .AddCookie(o =>
    {
        o.LoginPath = "/Account/Login";
        o.LogoutPath = "/Account/Logout";
        o.AccessDeniedPath = "/Account/AccessDenied";
        o.Cookie.Name = "pt_auth";
        o.ExpireTimeSpan = TimeSpan.FromDays(14);
        o.SlidingExpiration = true;
        o.Cookie.HttpOnly = true;
        o.Cookie.SameSite = SameSiteMode.Lax;
        o.Cookie.SecurePolicy = useSecureCookies ? CookieSecurePolicy.Always : CookieSecurePolicy.SameAsRequest;
    })
    .AddScheme<DeviceTokenOptions, DeviceTokenHandler>(DeviceTokenHandler.SchemeName, _ => { });

builder.Services.AddAuthorization(o =>
{
    o.AddPolicy("AnyUser", p =>
    {
        p.AddAuthenticationSchemes(CookieAuthenticationDefaults.AuthenticationScheme, DeviceTokenHandler.SchemeName);
        p.RequireAuthenticatedUser();
    });
    o.AddPolicy("Admin", p =>
    {
        p.AddAuthenticationSchemes(CookieAuthenticationDefaults.AuthenticationScheme);
        p.RequireAuthenticatedUser();
        p.Requirements.Add(new AdminRequirement());
    });
});

builder.Services.AddRazorPages(o =>
{
    o.Conventions.AuthorizeFolder("/Bank");
    o.Conventions.AuthorizeFolder("/Devices");
    o.Conventions.AuthorizeFolder("/Saves");
    o.Conventions.AuthorizeFolder("/Admin", "Admin");
});
builder.Services.AddControllers();
builder.Services.AddOpenApi();
builder.Services.AddHealthChecks().AddDbContextCheck<BankDbContext>();

var app = builder.Build();

app.UseForwardedHeaders();

using (var scope = app.Services.CreateScope())
{
    var db = scope.ServiceProvider.GetRequiredService<BankDbContext>();
    var bankOptions = scope.ServiceProvider.GetRequiredService<IOptions<BankOptions>>();
    await DatabaseBootstrap.InitializeAsync(db, bankOptions.Value);
}

if (!app.Environment.IsDevelopment())
    app.UseExceptionHandler("/Error");

app.UseStaticFiles();
app.UseRouting();
app.UseAuthentication();
app.UseAuthorization();
app.MapOpenApi();
app.MapHealthChecks("/health");
app.MapControllers();
app.MapRazorPages();

app.Run();

static bool IsPostgres(string connection)
{
    return connection.Contains("Host=", StringComparison.OrdinalIgnoreCase)
        || connection.Contains("Username=", StringComparison.OrdinalIgnoreCase)
        || connection.StartsWith("postgres", StringComparison.OrdinalIgnoreCase)
        || connection.Contains("Server=", StringComparison.OrdinalIgnoreCase) && connection.Contains("Port=", StringComparison.OrdinalIgnoreCase);
}
