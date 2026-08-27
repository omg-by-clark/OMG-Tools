param(
  [string]$Root,
  [int]$Port = 8766
)
Add-Type -AssemblyName System.Web
$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add("http://127.0.0.1:$Port/")
$listener.Start()
try {
  while ($listener.IsListening) {
    $context = $listener.GetContext()
    $requestPath = [System.Web.HttpUtility]::UrlDecode($context.Request.Url.AbsolutePath.TrimStart('/'))
    if ([string]::IsNullOrWhiteSpace($requestPath)) { $requestPath = 'index.html' }
    $fullPath = Join-Path $Root $requestPath
    if ((Test-Path $fullPath) -and (Get-Item $fullPath).PSIsContainer) {
      $fullPath = Join-Path $fullPath 'index.html'
    }
    if (-not (Test-Path $fullPath)) {
      $context.Response.StatusCode = 404
      $buffer = [Text.Encoding]::UTF8.GetBytes('Not Found')
      $context.Response.OutputStream.Write($buffer, 0, $buffer.Length)
      $context.Response.Close()
      continue
    }
    $extension = [IO.Path]::GetExtension($fullPath).ToLowerInvariant()
    $contentType = switch ($extension) {
      '.html' { 'text/html; charset=utf-8' }
      '.js'   { 'application/javascript; charset=utf-8' }
      '.css'  { 'text/css; charset=utf-8' }
      '.svg'  { 'image/svg+xml' }
      '.yaml' { 'text/yaml; charset=utf-8' }
      '.yml'  { 'text/yaml; charset=utf-8' }
      '.json' { 'application/json; charset=utf-8' }
      default { 'application/octet-stream' }
    }
    $bytes = [IO.File]::ReadAllBytes($fullPath)
    $context.Response.ContentType = $contentType
    $context.Response.ContentLength64 = $bytes.Length
    $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
    $context.Response.Close()
  }
} finally {
  $listener.Stop()
  $listener.Close()
}
