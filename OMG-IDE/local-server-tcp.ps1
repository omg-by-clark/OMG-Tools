param(
  [string]$Root = $PSScriptRoot,
  [int]$Port = 8765,
  [int]$CategoryId = 7
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName System.Web

$RemoteBase = 'http://123.60.188.246'
$script:RemoteToken = ''
$script:RemoteUsername = ''
$script:ProgrammingCache = $null
$script:ProgrammingCacheTime = [DateTime]::MinValue
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse('127.0.0.1'), $Port)

function Send-Response {
  param($Stream, [int]$StatusCode = 200, [string]$StatusText = 'OK', [string]$ContentType = 'text/plain; charset=utf-8', [byte[]]$Body = @())
  $header = "HTTP/1.1 $StatusCode $StatusText`r`nContent-Type: $ContentType`r`nContent-Length: $($Body.Length)`r`nCache-Control: no-store`r`nX-Content-Type-Options: nosniff`r`nConnection: close`r`n`r`n"
  $headerBytes = [Text.Encoding]::ASCII.GetBytes($header)
  $Stream.Write($headerBytes, 0, $headerBytes.Length)
  if ($Body.Length) { $Stream.Write($Body, 0, $Body.Length) }
  $Stream.Flush()
}

function Send-Json {
  param($Stream, $Data, [int]$StatusCode = 200, [string]$StatusText = 'OK')
  $bytes = [Text.Encoding]::UTF8.GetBytes(($Data | ConvertTo-Json -Depth 12 -Compress))
  Send-Response -Stream $Stream -StatusCode $StatusCode -StatusText $StatusText -ContentType 'application/json; charset=utf-8' -Body $bytes
}

function Read-HttpRequest {
  param($Stream)
  $headerBytes = New-Object System.Collections.Generic.List[byte]
  $tail = ''
  while ($tail -ne "`r`n`r`n") {
    $value = $Stream.ReadByte()
    if ($value -lt 0) { return $null }
    $headerBytes.Add([byte]$value)
    $tail = ($tail + [char]$value)
    if ($tail.Length -gt 4) { $tail = $tail.Substring($tail.Length - 4) }
    if ($headerBytes.Count -gt 65536) { throw '请求头过大' }
  }

  $headerText = [Text.Encoding]::ASCII.GetString($headerBytes.ToArray())
  $lines = $headerText -split "`r`n"
  $requestParts = $lines[0] -split ' '
  if ($requestParts.Count -lt 2) { throw '无效请求' }
  $headers = @{}
  foreach ($line in $lines[1..($lines.Count - 1)]) {
    if (-not $line) { continue }
    $parts = $line -split ':', 2
    if ($parts.Count -eq 2) { $headers[$parts[0].Trim().ToLowerInvariant()] = $parts[1].Trim() }
  }

  $contentLength = 0
  if ($headers.ContainsKey('content-length')) { [int]::TryParse($headers['content-length'], [ref]$contentLength) | Out-Null }
  if ($contentLength -gt 0 -and $headers.ContainsKey('expect') -and $headers['expect'].ToLowerInvariant().Contains('100-continue')) {
    $continueBytes = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 100 Continue`r`n`r`n")
    $Stream.Write($continueBytes, 0, $continueBytes.Length)
    $Stream.Flush()
  }
  $bodyBytes = New-Object byte[] $contentLength
  $offset = 0
  while ($offset -lt $contentLength) {
    $read = $Stream.Read($bodyBytes, $offset, $contentLength - $offset)
    if ($read -le 0) { break }
    $offset += $read
  }

  [pscustomobject]@{
    Method = $requestParts[0].ToUpperInvariant()
    Target = $requestParts[1]
    Headers = $headers
    Body = [Text.Encoding]::UTF8.GetString($bodyBytes, 0, $offset)
  }
}

function Invoke-Remote {
  param([string]$Path, [string]$Method = 'GET', [hashtable]$Query = @{}, [string]$JsonBody = '', [bool]$UseToken = $true)
  $builder = [UriBuilder]::new($RemoteBase.TrimEnd('/') + $Path)
  if ($Query.Count) {
    $pairs = foreach ($entry in $Query.GetEnumerator()) {
      '{0}={1}' -f [Web.HttpUtility]::UrlEncode([string]$entry.Key), [Web.HttpUtility]::UrlEncode([string]$entry.Value)
    }
    $builder.Query = [string]::Join('&', $pairs)
  }

  $request = [Net.HttpWebRequest]::Create($builder.Uri)
  $request.Method = $Method
  $request.UserAgent = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/138.0 Safari/537.36'
  $request.Accept = 'application/json'
  $request.Referer = "$RemoteBase/"
  $request.Timeout = 30000
  $request.ReadWriteTimeout = 30000
  $request.ServicePoint.Expect100Continue = $false
  $request.Headers.Add('Url-Type', 'general')
  $request.Headers.Add('Origin', $RemoteBase)
  $request.Headers.Add('Accept-Language', 'zh-CN,zh;q=0.9,en;q=0.7')
  if ($UseToken -and $script:RemoteToken) { $request.Headers.Add('Authorization', $script:RemoteToken) }
  if ($JsonBody) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($JsonBody)
    $request.ContentType = 'application/json; charset=utf-8'
    $request.ContentLength = $bytes.Length
    $requestStream = $request.GetRequestStream()
    try { $requestStream.Write($bytes, 0, $bytes.Length) } finally { $requestStream.Dispose() }
  } elseif ($Method -ne 'GET') {
    $request.ContentLength = 0
  }

  try {
    $response = [Net.HttpWebResponse]$request.GetResponse()
  } catch [Net.WebException] {
    if (-not $_.Exception.Response) {
      throw '无法连接目标题库，请检查网络后重试'
    }
    $response = [Net.HttpWebResponse]$_.Exception.Response
  }

  try {
    $reader = New-Object IO.StreamReader($response.GetResponseStream(), [Text.Encoding]::UTF8)
    try { $content = $reader.ReadToEnd() } finally { $reader.Dispose() }
    [pscustomobject]@{
      StatusCode = [int]$response.StatusCode
      Content = $content
      Authorization = [string]$response.Headers['Authorization']
    }
  } finally {
    $response.Dispose()
  }
}

function Get-RemoteData {
  param([string]$Path, [string]$Method = 'GET', [hashtable]$Query = @{}, [string]$JsonBody = '')
  $response = Invoke-Remote -Path $Path -Method $Method -Query $Query -JsonBody $JsonBody
  $payload = if ($response.Content) { $response.Content | ConvertFrom-Json } else { $null }
  if ($response.StatusCode -eq 401) { throw [UnauthorizedAccessException]::new('目标站登录已失效') }
  if ($response.StatusCode -lt 200 -or $response.StatusCode -ge 300 -or ($payload -and $payload.status -and $payload.status -ne 200)) {
    $message = if ($payload.msg) { $payload.msg } else { "目标站请求失败（HTTP $($response.StatusCode)）" }
    throw $message
  }
  return $payload.data
}

function Get-TrainingPassword {
  param([string]$Description)
  $match = [regex]::Match($Description, '密码\s*[：:]\s*([^\s\r\n]+)')
  if ($match.Success) { return $match.Groups[1].Value.Trim() }
  return ''
}

function Get-ProgrammingCatalog {
  if ($script:ProgrammingCache -and ([DateTime]::UtcNow - $script:ProgrammingCacheTime).TotalMinutes -lt 15) { return $script:ProgrammingCache }

  $trainings = New-Object System.Collections.Generic.List[object]
  $currentPage = 1
  $pages = 1
  do {
    $list = Get-RemoteData -Path '/api/get-training-list' -Query @{ currentPage = $currentPage; limit = 100; categoryId = $CategoryId }
    foreach ($training in @($list.records)) { $trainings.Add($training) }
    $pages = if ($list.pages) { [int]$list.pages } elseif ($list.total) { [Math]::Ceiling([double]$list.total / 100) } else { 1 }
    $currentPage += 1
  } while ($currentPage -le $pages -and $currentPage -le 100)
  $byPid = @{}
  $byProblemId = @{}

  foreach ($training in $trainings) {
    if ($training.auth -eq 'Private') {
      $password = Get-TrainingPassword ([string]$training.description)
      if (-not $password) { continue }
      $registerBody = @{ tid = [int]$training.id; password = $password } | ConvertTo-Json -Compress
      try { Get-RemoteData -Path '/api/register-training' -Method 'POST' -JsonBody $registerBody | Out-Null } catch [UnauthorizedAccessException] { throw } catch { }
    }

    try { $problemList = @(Get-RemoteData -Path '/api/get-training-problem-list' -Query @{ tid = $training.id }) } catch [UnauthorizedAccessException] { throw } catch { continue }
    foreach ($problem in $problemList) {
      $entry = [pscustomobject]@{
        pid = [string]$problem.pid
        problemId = [string]$problem.problemId
        title = [string]$problem.title
        difficulty = $problem.difficulty
        trainingId = [int]$training.id
        trainingTitle = [string]$training.title
      }
      if ($entry.pid) { $byPid[$entry.pid] = $entry }
      if ($entry.problemId) { $byProblemId[$entry.problemId] = $entry }
    }
  }

  $script:ProgrammingCache = [pscustomobject]@{
    TrainingCount = $trainings.Count
    ProblemCount = $byPid.Count
    ByPid = $byPid
    ByProblemId = $byProblemId
  }
  $script:ProgrammingCacheTime = [DateTime]::UtcNow
  return $script:ProgrammingCache
}

function Get-RankedSubmissions {
  $records = New-Object System.Collections.Generic.List[object]
  $currentPage = 1
  $pages = 1
  do {
    $data = Get-RemoteData -Path '/api/get-submission-list' -Query @{ currentPage = $currentPage; limit = 100 }
    foreach ($record in @($data.records)) { $records.Add($record) }
    $pages = if ($data.pages) { [int]$data.pages } elseif ($data.total) { [Math]::Ceiling([double]$data.total / 100) } else { 1 }
    $currentPage += 1
  } while ($currentPage -le $pages -and $currentPage -le 200)
  return $records.ToArray()
}

function Search-ProgrammingProblems {
  param([double]$MinScore, [string]$Keyword, [int]$Page, [int]$Limit)
  $catalog = Get-ProgrammingCatalog
  $submissions = @()
  try { $submissions = @(Get-RankedSubmissions) } catch { $submissions = @() }
  $unique = @{}
  $rankByKey = @{}

  foreach ($submission in $submissions) {
    if ($null -eq $submission.oiRankScore) { continue }
    $rankScore = [double]$submission.oiRankScore
    $keys = @()
    if ($submission.pid) { $keys += [string]$submission.pid }
    if ($submission.problemId) { $keys += [string]$submission.problemId }
    foreach ($key in $keys) {
      if (-not $rankByKey.ContainsKey($key) -or $rankScore -gt [double]$rankByKey[$key].oiRankScore) {
        $rankByKey[$key] = [pscustomobject]@{ score = $submission.score; oiRankScore = [Math]::Round($rankScore, 2) }
      }
    }
  }

  foreach ($problem in @($catalog.ByPid.Values)) {
    $searchText = "$($problem.problemId) $($problem.title) $($problem.trainingTitle)"
    if ($Keyword -and $searchText.IndexOf($Keyword, [StringComparison]::OrdinalIgnoreCase) -lt 0) { continue }
    $key = if ($problem.pid) { $problem.pid } else { $problem.problemId }
    $rank = $null
    if ($problem.pid -and $rankByKey.ContainsKey([string]$problem.pid)) { $rank = $rankByKey[[string]$problem.pid] }
    elseif ($problem.problemId -and $rankByKey.ContainsKey([string]$problem.problemId)) { $rank = $rankByKey[[string]$problem.problemId] }
    $rankScore = if ($rank) { [double]$rank.oiRankScore } else { 0.0 }
    if ($rankScore -lt $MinScore) { continue }
    if (-not $unique.ContainsKey($key)) {
      $unique[$key] = [pscustomobject]@{
        problemId = $problem.problemId
        title = $problem.title
        trainingId = $problem.trainingId
        trainingTitle = $problem.trainingTitle
        score = if ($rank) { $rank.score } else { $null }
        oiRankScore = [Math]::Round($rankScore, 2)
        url = "$RemoteBase/training/$($problem.trainingId)/problem/$([Uri]::EscapeDataString($problem.problemId))"
      }
    }
  }

  $sorted = @($unique.Values | Sort-Object @{ Expression = { [double]$_.oiRankScore }; Descending = $true }, problemId)
  $total = $sorted.Count
  $pages = [Math]::Max(1, [Math]::Ceiling($total / [double]$Limit))
  $safePage = [Math]::Max(1, [Math]::Min($Page, $pages))
  $start = ($safePage - 1) * $Limit
  $pageRecords = @()
  for ($index = $start; $index -lt [Math]::Min($start + $Limit, $total); $index++) { $pageRecords += $sorted[$index] }

  [pscustomobject]@{
    records = $pageRecords
    total = $total
    pages = $pages
    page = $safePage
    limit = $Limit
    trainingCount = $catalog.TrainingCount
    problemCount = $catalog.ProblemCount
  }
}

function Serve-StaticFile {
  param($Stream, [string]$RequestPath)
  $path = [Web.HttpUtility]::UrlDecode($RequestPath.TrimStart('/'))
  if (-not $path) { $path = 'index.html' }
  $basePath = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  $fullPath = [IO.Path]::GetFullPath((Join-Path $Root $path))
  if (-not $fullPath.StartsWith($basePath, [StringComparison]::OrdinalIgnoreCase)) { Send-Response -Stream $Stream -StatusCode 403 -StatusText 'Forbidden' -Body ([Text.Encoding]::UTF8.GetBytes('Forbidden')); return }
  if ((Test-Path -LiteralPath $fullPath) -and (Get-Item -LiteralPath $fullPath).PSIsContainer) { $fullPath = Join-Path $fullPath 'index.html' }
  if (-not (Test-Path -LiteralPath $fullPath)) { Send-Response -Stream $Stream -StatusCode 404 -StatusText 'Not Found' -Body ([Text.Encoding]::UTF8.GetBytes('Not Found')); return }
  $contentType = switch ([IO.Path]::GetExtension($fullPath).ToLowerInvariant()) {
    '.html' { 'text/html; charset=utf-8' } '.js' { 'application/javascript; charset=utf-8' } '.css' { 'text/css; charset=utf-8' }
    '.svg' { 'image/svg+xml' } '.yaml' { 'text/yaml; charset=utf-8' } '.yml' { 'text/yaml; charset=utf-8' } '.json' { 'application/json; charset=utf-8' }
    default { 'application/octet-stream' }
  }
  Send-Response -Stream $Stream -ContentType $contentType -Body ([IO.File]::ReadAllBytes($fullPath))
}

$listener.Start()
Write-Host "Online Exercises is running on http://127.0.0.1:$Port/"
try {
  while ($true) {
    $client = $listener.AcceptTcpClient()
    try {
      $client.ReceiveTimeout = 15000
      $client.SendTimeout = 15000
      $stream = $client.GetStream()
      $request = Read-HttpRequest $stream
      if (-not $request) { continue }
      $uri = [Uri]::new("http://127.0.0.1:$Port$($request.Target)")
      $path = $uri.AbsolutePath
      $query = [Web.HttpUtility]::ParseQueryString($uri.Query)
      try {
        switch ($path) {
          '/api/programming/status' {
            Send-Json -Stream $stream -Data @{ authenticated = [bool]$script:RemoteToken; username = $script:RemoteUsername }
          }
          '/api/programming/login' {
            if ($request.Method -ne 'POST') { Send-Json -Stream $stream -StatusCode 405 -StatusText 'Method Not Allowed' -Data @{ error = '仅支持 POST' }; continue }
            $credentials = $request.Body | ConvertFrom-Json
            if (-not $credentials.username -or -not $credentials.password) { Send-Json -Stream $stream -StatusCode 400 -StatusText 'Bad Request' -Data @{ error = '请输入用户名和密码' }; continue }
            $response = Invoke-Remote -Path '/api/login' -Method 'POST' -JsonBody (@{ username = [string]$credentials.username; password = [string]$credentials.password } | ConvertTo-Json -Compress) -UseToken $false
            $payload = if ($response.Content) { $response.Content | ConvertFrom-Json } else { $null }
            if ($response.StatusCode -ne 200) {
              $remoteMessage = if ($payload.msg) { [string]$payload.msg } else { "HTTP $($response.StatusCode)" }
              Send-Json -Stream $stream -StatusCode 401 -StatusText 'Unauthorized' -Data @{ error = "目标站拒绝登录：$remoteMessage。请确认填写的是网站账号密码，不是课程密码" }
              continue
            }
            if (-not $response.Authorization) {
              Send-Json -Stream $stream -StatusCode 502 -StatusText 'Bad Gateway' -Data @{ error = '账号密码已被目标站接受，但响应中没有登录令牌；这不是密码错误' }
              continue
            }
            $script:RemoteToken = $response.Authorization
            $script:RemoteUsername = if ($payload.data.username) { [string]$payload.data.username } else { [string]$credentials.username }
            $script:ProgrammingCache = $null
            Send-Json -Stream $stream -Data @{ authenticated = $true; username = $script:RemoteUsername }
          }
          '/api/programming/logout' {
            $script:RemoteToken = ''; $script:RemoteUsername = ''; $script:ProgrammingCache = $null
            Send-Json -Stream $stream -Data @{ authenticated = $false }
          }
          '/api/programming/search' {
            if (-not $script:RemoteToken) { Send-Json -Stream $stream -StatusCode 401 -StatusText 'Unauthorized' -Data @{ error = '请先连接目标站账号' }; continue }
            $minScore = 10.0; [double]::TryParse($query['minScore'], [ref]$minScore) | Out-Null
            $page = 1; [int]::TryParse($query['page'], [ref]$page) | Out-Null
            $limit = 12; [int]::TryParse($query['limit'], [ref]$limit) | Out-Null
            $limit = [Math]::Max(1, [Math]::Min(2000, $limit))
            Send-Json -Stream $stream -Data (Search-ProgrammingProblems -MinScore ([Math]::Max(0, $minScore)) -Keyword ([string]$query['query']) -Page $page -Limit $limit)
          }
          '/api/programming/problem' {
            if ($request.Method -ne 'GET') { Send-Json -Stream $stream -StatusCode 405 -StatusText 'Method Not Allowed' -Data @{ error = '仅支持 GET' }; continue }
            if (-not $script:RemoteToken) { Send-Json -Stream $stream -StatusCode 401 -StatusText 'Unauthorized' -Data @{ error = '请先连接目标站账号' }; continue }
            $trainingId = 0; [int]::TryParse($query['trainingId'], [ref]$trainingId) | Out-Null
            $problemId = [string]$query['problemId']
            if ($trainingId -le 0 -or -not $problemId) { Send-Json -Stream $stream -StatusCode 400 -StatusText 'Bad Request' -Data @{ error = '缺少训练编号或题号' }; continue }
            Get-ProgrammingCatalog | Out-Null
            $problemData = Get-RemoteData -Path '/api/get-problem-detail' -Query @{ problemId = $problemId }
            Send-Json -Stream $stream -Data $problemData
          }
          '/api/programming/submit' {
            if ($request.Method -ne 'POST') { Send-Json -Stream $stream -StatusCode 405 -StatusText 'Method Not Allowed' -Data @{ error = '仅支持 POST' }; continue }
            if (-not $script:RemoteToken) { Send-Json -Stream $stream -StatusCode 401 -StatusText 'Unauthorized' -Data @{ error = '请先连接目标站账号' }; continue }
            $submission = $request.Body | ConvertFrom-Json
            $trainingId = 0; [int]::TryParse([string]$submission.trainingId, [ref]$trainingId) | Out-Null
            $problemId = [string]$submission.problemId
            $language = [string]$submission.language
            $code = [string]$submission.code
            if ($trainingId -le 0 -or -not $problemId -or -not $language -or -not $code.Trim()) { Send-Json -Stream $stream -StatusCode 400 -StatusText 'Bad Request' -Data @{ error = '题目、语言或代码不完整' }; continue }
            if ($code.Length -gt 65535) { Send-Json -Stream $stream -StatusCode 400 -StatusText 'Bad Request' -Data @{ error = '代码不能超过 65535 个字符' }; continue }
            $remoteBody = @{
              pid = $problemId
              language = $language
              code = $code
              cid = $null
              tid = $trainingId
              gid = $null
              isRemote = [bool]$submission.isRemote
            } | ConvertTo-Json -Depth 5 -Compress
            $submitData = Get-RemoteData -Path '/api/submit-problem-judge' -Method 'POST' -JsonBody $remoteBody
            if (-not $submitData.submitId) { throw '目标站没有返回提交编号' }
            Send-Json -Stream $stream -Data @{ submitId = $submitData.submitId }
          }
          '/api/programming/submission' {
            if ($request.Method -ne 'GET') { Send-Json -Stream $stream -StatusCode 405 -StatusText 'Method Not Allowed' -Data @{ error = '仅支持 GET' }; continue }
            if (-not $script:RemoteToken) { Send-Json -Stream $stream -StatusCode 401 -StatusText 'Unauthorized' -Data @{ error = '请先连接目标站账号' }; continue }
            $submitId = [string]$query['submitId']
            if (-not $submitId) { Send-Json -Stream $stream -StatusCode 400 -StatusText 'Bad Request' -Data @{ error = '缺少提交编号' }; continue }
            $submissionData = Get-RemoteData -Path '/api/get-submission-detail' -Query @{ submitId = $submitId }
            Send-Json -Stream $stream -Data $submissionData
          }
          default { Serve-StaticFile -Stream $stream -RequestPath $path }
        }
      } catch [UnauthorizedAccessException] {
        $script:RemoteToken = ''; $script:RemoteUsername = ''; $script:ProgrammingCache = $null
        Send-Json -Stream $stream -StatusCode 401 -StatusText 'Unauthorized' -Data @{ error = $_.Exception.Message }
      } catch {
        $message = if ($_.Exception.Message) { $_.Exception.Message } else { $_.ToString() }
        Send-Json -Stream $stream -StatusCode 500 -StatusText 'Internal Server Error' -Data @{ error = $message }
      }
    } catch {
      # 浏览器可能只建立预连接而不发送请求。单个连接超时或中断时，
      # 只关闭当前连接，不能让整个本地服务退出。
    } finally {
      $client.Close()
    }
  }
} finally {
  $listener.Stop()
}
