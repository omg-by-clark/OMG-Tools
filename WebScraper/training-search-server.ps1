param(
  [string]$Root = (Join-Path $PSScriptRoot 'training-search'),
  [int]$Port = 8765,
  [int]$CategoryId = 7
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName System.Web

$RemoteBase = 'http://123.60.188.246'
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse('127.0.0.1'), $Port)

function Send-Response {
  param(
    [Parameter(Mandatory = $true)]$Stream,
    [int]$StatusCode = 200,
    [string]$StatusText = 'OK',
    [string]$ContentType = 'text/plain; charset=utf-8',
    [byte[]]$Body = @()
  )

  $header = "HTTP/1.1 $StatusCode $StatusText`r`nContent-Type: $ContentType`r`nContent-Length: $($Body.Length)`r`nConnection: close`r`n`r`n"
  $headerBytes = [System.Text.Encoding]::ASCII.GetBytes($header)
  $Stream.Write($headerBytes, 0, $headerBytes.Length)
  if ($Body.Length -gt 0) {
    $Stream.Write($Body, 0, $Body.Length)
  }
  $Stream.Flush()
}

function Send-Json {
  param(
    [Parameter(Mandatory = $true)]$Stream,
    [Parameter(Mandatory = $true)]$Data,
    [int]$StatusCode = 200,
    [string]$StatusText = 'OK'
  )

  $json = $Data | ConvertTo-Json -Depth 8 -Compress
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
  Send-Response -Stream $Stream -StatusCode $StatusCode -StatusText $StatusText -ContentType 'application/json; charset=utf-8' -Body $bytes
}

function Invoke-RemoteJson {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [hashtable]$Query = @{}
  )

  $builder = [System.UriBuilder]::new($RemoteBase.TrimEnd('/') + $Path)
  if ($Query.Count -gt 0) {
    $pairs = foreach ($entry in $Query.GetEnumerator()) {
      '{0}={1}' -f [System.Web.HttpUtility]::UrlEncode([string]$entry.Key), [System.Web.HttpUtility]::UrlEncode([string]$entry.Value)
    }
    $builder.Query = [string]::Join('&', $pairs)
  }

  $client = New-Object System.Net.WebClient
  $client.Headers['User-Agent'] = 'OMG-Tools-TrainingSearch/1.0'
  $bytes = $client.DownloadData($builder.Uri.AbsoluteUri)
  $text = [System.Text.Encoding]::UTF8.GetString($bytes)
  return $text | ConvertFrom-Json
}

function Get-TrainingList {
  $payload = Invoke-RemoteJson -Path '/api/get-training-list' -Query @{
    currentPage = 1
    limit = 100
    categoryId = $CategoryId
  }

  return @($payload.data.records | ForEach-Object {
    [pscustomobject]@{
      id = $_.id
      title = $_.title
      auth = $_.auth
      rank = $_.rank
      problemCount = $_.problemCount
      categoryName = $_.categoryName
      categoryColor = $_.categoryColor
    }
  })
}

function Serve-StaticFile {
  param(
    [Parameter(Mandatory = $true)]$Stream,
    [Parameter(Mandatory = $true)][string]$BaseRoot,
    [Parameter(Mandatory = $true)][string]$RequestPath
  )

  $path = [System.Web.HttpUtility]::UrlDecode($RequestPath.TrimStart('/'))
  if ([string]::IsNullOrWhiteSpace($path)) {
    $path = 'index.html'
  }
  $fullPath = Join-Path $BaseRoot $path
  if ((Test-Path $fullPath) -and (Get-Item $fullPath).PSIsContainer) {
    $fullPath = Join-Path $fullPath 'index.html'
  }
  if (-not (Test-Path $fullPath)) {
    $body = [System.Text.Encoding]::UTF8.GetBytes('Not Found')
    Send-Response -Stream $Stream -StatusCode 404 -StatusText 'Not Found' -Body $body
    return
  }

  $extension = [IO.Path]::GetExtension($fullPath).ToLowerInvariant()
  $contentType = switch ($extension) {
    '.html' { 'text/html; charset=utf-8' }
    '.js'   { 'application/javascript; charset=utf-8' }
    '.css'  { 'text/css; charset=utf-8' }
    '.svg'  { 'image/svg+xml' }
    '.json' { 'application/json; charset=utf-8' }
    default { 'application/octet-stream' }
  }

  $bytes = [IO.File]::ReadAllBytes($fullPath)
  Send-Response -Stream $Stream -StatusCode 200 -StatusText 'OK' -ContentType $contentType -Body $bytes
}

$listener.Start()
Write-Host "Training search TCP server is running on http://127.0.0.1:$Port/"

try {
  while ($true) {
    $client = $listener.AcceptTcpClient()
    try {
      $stream = $client.GetStream()
      $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::ASCII, $false, 1024, $true)
      $requestLine = $reader.ReadLine()
      if (-not $requestLine) {
        $client.Close()
        continue
      }

      $headers = @{}
      while (($line = $reader.ReadLine()) -ne '') {
        if ($null -eq $line) { break }
        $parts = $line -split ':', 2
        if ($parts.Count -eq 2) {
          $headers[$parts[0].Trim()] = $parts[1].Trim()
        }
      }

      $segments = $requestLine -split ' '
      if ($segments.Count -lt 2) {
        $body = [System.Text.Encoding]::UTF8.GetBytes('Bad Request')
        Send-Response -Stream $stream -StatusCode 400 -StatusText 'Bad Request' -Body $body
        continue
      }

      $rawPath = $segments[1]
      $uri = [System.Uri]::new("http://127.0.0.1:$Port$rawPath")
      $path = $uri.AbsolutePath

      try {
        switch ($path) {
          '/api/status' {
            Send-Json -Stream $stream -Data @{
              port = $Port
              categoryId = $CategoryId
              privateAccess = $false
              date = '2026-07-19'
            }
            continue
          }
          '/api/trainings' {
            Send-Json -Stream $stream -Data @{
              trainings = Get-TrainingList
            }
            continue
          }
          default {
            Serve-StaticFile -Stream $stream -BaseRoot $Root -RequestPath $path
            continue
          }
        }
      } catch {
        Send-Json -Stream $stream -StatusCode 500 -StatusText 'Internal Server Error' -Data @{
          error = $_.Exception.Message
        }
      }
    } finally {
      $client.Close()
    }
  }
} finally {
  $listener.Stop()
}
