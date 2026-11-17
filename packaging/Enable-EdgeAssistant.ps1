$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$title = '启用 Eego 下载助手'
$folder = Join-Path $PSScriptRoot 'extension'
$message = @"
点击“确定”将打开 Edge 扩展页，并把以下目录复制到剪贴板：

$folder

1. 打开“开发人员模式”，点击“加载解压缩的扩展”。
2. 在文件夹选择窗口按 Ctrl+V 粘贴目录，然后点击“选择文件夹”。
3. 打开“Eego 下载助手”，勾选“尝试接管下载”。

已从此目录加载过助手的用户可以点“取消”。
"@
if ([Windows.Forms.MessageBox]::Show($message, $title, 'OKCancel', 'Information') -ne 'OK') { return }
try {
    [Windows.Forms.Clipboard]::SetText($folder)
    Start-Process -FilePath 'msedge.exe' -ArgumentList 'edge://extensions/'
} catch {
    [void][Windows.Forms.MessageBox]::Show("请打开 Edge，在地址栏输入 edge://extensions/，加载以下目录：$folder", $title)
}
