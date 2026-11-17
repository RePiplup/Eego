# Read the accessibility tree of the single test window passed by the harness.
Add-Type -AssemblyName UIAutomationClient
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
while ($line = [Console]::ReadLine()) {
    try {
        $req = $line | ConvertFrom-Json
        $w = [Windows.Automation.AutomationElement]::FromHandle([IntPtr]::new([long]$req.window))
        $nodes = $w.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
        $items = @(foreach ($n in $nodes) {
            $c = $n.Current
            $rect = $c.BoundingRectangle
            if ($rect.IsEmpty) {continue}
            $val = $null
            if ($c.ControlType.ProgrammaticName -eq 'ControlType.Edit') {
                $pat = $null
                if ($n.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern,[ref]$pat)) {
                    if ($req.operation -eq 'type' -and !$pat.Current.IsReadOnly -and $c.Name -eq '下载链接') {
                        $pat.SetValue([string]$req.value)
                    }
                    $val = $pat.Current.Value
                }
            }
            [pscustomobject]@{name=$c.Name;type=$c.ControlType.ProgrammaticName;enabled=$c.IsEnabled;x=$rect.X;y=$rect.Y;width=$rect.Width;height=$rect.Height;value=$val}
        })
        [Console]::WriteLine((ConvertTo-Json -InputObject $items -Depth 3 -Compress))
    } catch { [Console]::WriteLine((ConvertTo-Json -InputObject @{error=$_.Exception.Message} -Compress)) }
}
