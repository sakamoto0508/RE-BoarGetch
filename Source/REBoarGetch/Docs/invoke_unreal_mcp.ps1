param([string]$Name='list_toolsets',[string]$ArgsJson='{}')
$headers=@{Accept='application/json, text/event-stream'}
function Send-Mcp($body) {
 $response=Invoke-WebRequest -Uri 'http://127.0.0.1:8000/mcp' -Method Post -ContentType 'application/json' -Headers $headers -Body ($body|ConvertTo-Json -Depth 40 -Compress)
 if($response.Headers['Mcp-Session-Id']){$headers['Mcp-Session-Id']=[string]$response.Headers['Mcp-Session-Id'][0]}
 $s=$response.Content
 if($s -match '(?m)^data: (.+)$'){$s=$Matches[1]}
 return ($s|ConvertFrom-Json -Depth 60)
}
$null=Send-Mcp @{jsonrpc='2.0';id=1;method='initialize';params=@{protocolVersion='2024-11-05';capabilities=@{};clientInfo=@{name='Codex';version='1.0'}}}
$r=Send-Mcp @{jsonrpc='2.0';id=2;method='tools/call';params=@{name=$Name;arguments=($ArgsJson|ConvertFrom-Json)}}
$r|ConvertTo-Json -Depth 60 -Compress
