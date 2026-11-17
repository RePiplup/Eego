$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root ('artifacts/test-runs/real-response-' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $output | Out-Null
$record = Get-Content -LiteralPath (Join-Path $root 'dist/HandmadeBT-0.7-task-management-win64/userdata/tasks.record')
$urls = @($record | ForEach-Object { if ($_ -match '^\d+ \d+ \d+ \d+ "([^"]+)"') { $matches[1] } } | Select-Object -Unique)
$results = @()
foreach ($initial in $urls) {
    $current = [Uri]$initial
    $chain = @()
    for ($hop = 0; $hop -le 10; $hop++) {
        $request = New-Object -ComObject WinHttp.WinHttpRequest.5.1
        $request.Open('GET', $current.AbsoluteUri, $false)
        $request.Option(6) = $false
        $request.SetAutoLogonPolicy(2)
        $request.SetTimeouts(5000,5000,10000,10000)
        $request.SetRequestHeader('User-Agent','HandmadeBT/0.8.1')
        $request.SetRequestHeader('Accept-Encoding','identity')
        $request.SetRequestHeader('Range','bytes=0-0')
        $request.Send()
        $entry = @{url=$current.AbsoluteUri; status=$request.Status; headers=$request.GetAllResponseHeaders()}
        $chain += $entry
        if ($request.Status -in 301,302,303,307,308) {
            $next = [Uri]::new($current, $request.GetResponseHeader('Location'))
            if ($next.Scheme -notin 'http','https' -or ($current.Scheme -eq 'https' -and $next.Scheme -ne 'https') -or $next.UserInfo) { throw 'Unsafe redirect refused' }
            $current = $next
        } else {
            $body = Join-Path $output ('response-' + $results.Count + '.body')
            [IO.File]::WriteAllBytes($body, [byte[]]$request.ResponseBody)
            $entry.body = $body
            $entry.sha256 = (Get-FileHash -LiteralPath $body -Algorithm SHA256).Hash
            break
        }
    }
    $results += @{initial=$initial;chain=$chain}
}
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'responses.json') -Encoding UTF8
Write-Output $output
foreach ($result in $results) { foreach ($step in $result.chain) { Write-Output ($step.status.ToString()+' '+$step.url) } }
