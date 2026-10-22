@echo off
:: Rod Macro - double-click to run. Settings are saved next to this file (RodMacro.cfg).
set "LCK_DIR=%~dp0"
powershell -STA -WindowStyle Hidden -NoProfile -ExecutionPolicy Bypass -Command "iex ((Get-Content '%~f0' -Raw) -split ('#'+'#PS'))[1]"
exit /b

##PS
try {
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

public static class M {
  [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] struct UNI { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public UNI u; }

  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] inp, int size);
  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);
  [DllImport("user32.dll")] static extern bool ReleaseCapture();
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, int m, int w, int l);
  [DllImport("gdi32.dll")] static extern IntPtr CreateRoundRectRgn(int a, int b, int c, int d, int e, int f);
  [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint ms);

  public static volatile int Trig = 90, VK1 = 49, VK2 = 50, Hold = 20, Sync = 5, DSword = 69, DRod = 69;
  public static volatile string C1 = "N", C2 = "R";
  public static volatile bool On = true, Active = false;

  public static bool Down(int v) { return (GetAsyncKeyState(v) & 0x8000) != 0; }
  public static void Drag(IntPtr h) { ReleaseCapture(); SendMessage(h, 0xA1, 2, 0); }
  public static IntPtr Round(int w, int h, int r) { return CreateRoundRectRgn(0, 0, w + 1, h + 1, r, r); }

  public static int FirstDown(bool mouse) {
    for (int v = (mouse ? 4 : 8); v < 255; v++) {
      if (v == 16 || v == 17 || v == 18) continue;
      if (Down(v)) return v;
    }
    return 0;
  }

  static void Key(ushort sc, bool up) {
    INPUT[] i = new INPUT[1];
    i[0].type = 1;
    i[0].u.ki.wScan = sc;
    i[0].u.ki.dwFlags = up ? 0xAu : 0x8u;
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }

  static void Mouse(uint flag) {
    INPUT[] i = new INPUT[1];
    i[0].type = 0;
    i[0].u.mi.dwFlags = flag;
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }

  static void Step(int vk, string c, int hold, int sync) {
    ushort sc = (ushort)MapVirtualKey((uint)vk, 0);
    uint d = 0, u = 0;
    bool has = false;
    if (c == "L") { d = 0x2; u = 0x4; has = true; }
    else if (c == "R") { d = 0x8; u = 0x10; has = true; }
    Key(sc, false);
    if (has) {
      if (sync > 0) Thread.Sleep(sync);
      Mouse(d);
      Thread.Sleep(hold);
      Mouse(u);
    } else {
      Thread.Sleep(hold);
    }
    Key(sc, true);
  }

  static bool Held() { return On && Down(Trig); }

  static bool WaitUntil(Stopwatch sw, int t) {
    while (sw.ElapsedMilliseconds < t) {
      if (!Held()) return false;
      Thread.Sleep(1);
    }
    return Held();
  }

  static void Loop() {
    timeBeginPeriod(1);
    while (true) {
      if (!Held()) { Active = false; Thread.Sleep(2); continue; }
      Active = true;
      int ds = DSword, dr = DRod, sy = Sync;
      int h = Math.Max(1, Math.Min(Hold, Math.Min(ds, dr) - sy - 1));
      Stopwatch sw = Stopwatch.StartNew();
      Step(VK1, C1, h, sy);
      if (!WaitUntil(sw, ds)) continue;
      Step(VK2, C2, h, sy);
      WaitUntil(sw, ds + dr);
    }
  }

  public static void Start() {
    Thread t = new Thread(Loop);
    t.IsBackground = true;
    t.Start();
  }
}
"@

[Windows.Forms.Application]::EnableVisualStyles()

# ---------- config ----------
$defaults = [ordered]@{ Trig=90; K1=49; K2=50; Tog=119; C1='N'; C2='R'; Hold=20; Sync=5; D1=69; D2=69 }
$cfg = @{}
foreach ($k in $defaults.Keys) { $cfg[$k] = $defaults[$k] }
$dir = $env:LCK_DIR; if (-not $dir) { $dir = $env:TEMP }
$cfgPath = Join-Path $dir 'RodMacro.cfg'
$loadPath = $cfgPath
if (-not (Test-Path $cfgPath)) { $old = Join-Path $dir 'LOLICEKOS.cfg'; if (Test-Path $old) { $loadPath = $old } }
function LogErr($m) { try { Add-Content -Path (Join-Path $dir 'RodMacro_error.log') -Value ("{0}  {1}" -f (Get-Date), $m) } catch {} }
if (Test-Path $loadPath) {
  foreach ($line in (Get-Content $loadPath)) {
    $p = $line -split '=', 2
    if ($p.Count -eq 2 -and $cfg.ContainsKey($p[0].Trim())) {
      $key = $p[0].Trim(); $val = $p[1].Trim()
      if ($key -eq 'C1' -or $key -eq 'C2') { if ('L','R','N' -contains $val) { $cfg[$key] = $val } }
      else { $n = 0; if ([int]::TryParse($val, [ref]$n)) { $cfg[$key] = $n } }
    }
  }
}
function Save { Set-Content -Path $cfgPath -Value ($cfg.Keys | ForEach-Object { "$_=$($cfg[$_])" }) -Encoding ASCII }
function Apply {
  [M]::Trig = [int]$cfg.Trig; [M]::VK1 = [int]$cfg.K1; [M]::VK2 = [int]$cfg.K2
  [M]::Hold = [int]$cfg.Hold; [M]::Sync = [int]$cfg.Sync
  [M]::DSword = [int]$cfg.D1; [M]::DRod = [int]$cfg.D2
  [M]::C1 = [string]$cfg.C1; [M]::C2 = [string]$cfg.C2
}
function KN([int]$v) {
  switch ($v) {
    0 { 'None' } 1 { 'Left Mouse' } 2 { 'Right Mouse' } 4 { 'Middle Mouse' }
    5 { 'Mouse 4' } 6 { 'Mouse 5' }
    default {
      $n = ([Windows.Forms.Keys]$v).ToString()
      if ($n -match '^D(\d)$') { $Matches[1] } elseif ($n -match '^NumPad(\d)$') { 'Num ' + $Matches[1] } else { $n }
    }
  }
}

# ---------- theme ----------
function RGB($r, $g, $b) { [Drawing.Color]::FromArgb($r, $g, $b) }
function Mix($a, $b, $t) { [Drawing.Color]::FromArgb([int]($a.R + ($b.R - $a.R) * $t), [int]($a.G + ($b.G - $a.G) * $t), [int]($a.B + ($b.B - $a.B) * $t)) }
$cBg = RGB 14 15 22; $cPanel = RGB 28 30 44; $cHover = RGB 46 49 74
$cTxt = RGB 228 231 242; $cDim = RGB 120 126 152
$cA = RGB 0 220 255; $cB = RGB 150 80 255; $cC = RGB 255 70 170
$cG = RGB 70 235 150; $cR = RGB 255 85 100; $cY = RGB 255 200 70
$fN = New-Object Drawing.Font('Segoe UI', 9.5)
$fS = New-Object Drawing.Font('Segoe UI Semibold', 9.5)
$fCap = New-Object Drawing.Font('Segoe UI Semibold', 8)
$fSmall = New-Object Drawing.Font('Segoe UI', 8.5)
$fT = New-Object Drawing.Font('Segoe UI Black', 15)
$fBig = New-Object Drawing.Font('Segoe UI Semibold', 14)
$dimBrush = New-Object Drawing.SolidBrush $cDim
$lineBrush = New-Object Drawing.SolidBrush $cPanel
$shine = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(120, 255, 255, 255))

$global:ph = 0.0; $global:rp = 0.0; $global:state = 1; $global:cap = $null; $global:wait = $false
$global:loading = $false; $global:togPrev = $false; $global:savedAt = $null
$global:stTxt = 'READY'; $global:hint = ''; $global:stCol = $cY
$global:anim = New-Object Collections.ArrayList

# ---------- form ----------
$FW = 380; $FH = 552
$form = New-Object Windows.Forms.Form
$form.FormBorderStyle = 'None'; $form.StartPosition = 'CenterScreen'
$form.ClientSize = New-Object Drawing.Size($FW, $FH)
$form.BackColor = $cBg; $form.Text = 'Rod Macro'; $form.TopMost = $true
$form.GetType().GetProperty('DoubleBuffered', [Reflection.BindingFlags]'Instance,NonPublic').SetValue($form, $true, $null)
$form.Region = [Drawing.Region]::FromHrgn([M]::Round($FW, $FH, 26))
$tip = New-Object Windows.Forms.ToolTip

function Lbl($t, $x, $y, $w, $h = 22, $col = $cTxt, $font = $fN) {
  $l = New-Object Windows.Forms.Label
  $l.Text = $t; $l.Location = New-Object Drawing.Point($x, $y); $l.Size = New-Object Drawing.Size($w, $h)
  $l.ForeColor = $col; $l.BackColor = $cBg; $l.Font = $font; $l.TextAlign = 'MiddleLeft'
  $form.Controls.Add($l); $l
}
function Sec($t, $y) { [void](Lbl $t 24 $y 200 16 $cDim $fCap) }
function Pill($t, $x, $y, $w, $h = 28) {
  $b = New-Object Windows.Forms.Button
  $b.Text = $t; $b.Location = New-Object Drawing.Point($x, $y); $b.Size = New-Object Drawing.Size($w, $h)
  $b.FlatStyle = 'Flat'; $b.FlatAppearance.BorderSize = 0
  $b.BackColor = $cPanel; $b.ForeColor = $cTxt; $b.Font = $fS; $b.Cursor = 'Hand'; $b.TabStop = $false
  $b.FlatAppearance.MouseOverBackColor = $cPanel; $b.FlatAppearance.MouseDownBackColor = $cPanel
  $b.Region = [Drawing.Region]::FromHrgn([M]::Round($w, $h, 14))
  $form.Controls.Add($b)
  [void]$global:anim.Add(@{ b = $b; h = 0.0; base = $cPanel; hov = $cHover })
  $b
}

# window buttons
$bx = Lbl 'X' 346 10 24 24 $cDim $fS; $bx.TextAlign = 'MiddleCenter'; $bx.Cursor = 'Hand'
$bx.Add_Click({ $form.Close() })
$bx.Add_MouseEnter({ param($s, $e) $s.ForeColor = $cR }); $bx.Add_MouseLeave({ param($s, $e) $s.ForeColor = $cDim })
$bm = Lbl '-' 320 10 24 24 $cDim $fS; $bm.TextAlign = 'MiddleCenter'; $bm.Cursor = 'Hand'
$bm.Add_Click({ $form.WindowState = 'Minimized' })
$bm.Add_MouseEnter({ param($s, $e) $s.ForeColor = $cTxt }); $bm.Add_MouseLeave({ param($s, $e) $s.ForeColor = $cDim })

# ---------- keybinds ----------
Sec 'KEYBINDS' 120
$btns = @{}
$rows = @(@('Trig', 'Trigger (hold to run)'), @('K1', 'Slot 1 key (sword)'), @('K2', 'Slot 2 key (rod)'), @('Tog', 'Macro on / off key'))
$y = 140
foreach ($r in $rows) {
  [void](Lbl $r[1] 24 $y 180 28)
  $b = Pill '' 214 $y 142 28
  $b.Tag = $r[0]
  $b.Add_Click({ param($s, $e) StartCap $s.Tag })
  $btns[$r[0]] = $b
  $y += 34
}

# ---------- clicks ----------
Sec 'CLICKS  (click to change)' 284
$cbtns = @{}
$cy = 304
foreach ($r in @(@('C1', 'Slot 1 click'), @('C2', 'Slot 2 click'))) {
  [void](Lbl $r[1] 24 $cy 180 28)
  $b = Pill '' 214 $cy 142 28
  $b.Tag = $r[0]
  $b.Add_Click({ param($s, $e)
    $o = @('L', 'R', 'N'); $i = [Array]::IndexOf($o, [string]$cfg[$s.Tag])
    $cfg[$s.Tag] = $o[($i + 1) % 3]; Apply; Refresh })
  $cbtns[$r[0]] = $b
  $cy += 34
}

# ---------- timing ----------
Sec 'TIMING  (ms)' 380
$nums = @{}
function Num($x, $y, $key, $min) {
  $n = New-Object Windows.Forms.NumericUpDown
  $n.Minimum = $min; $n.Maximum = 2000; $n.BackColor = $cPanel; $n.ForeColor = $cTxt; $n.Font = $fN; $n.BorderStyle = 'FixedSingle'
  $n.Location = New-Object Drawing.Point($x, $y); $n.Size = New-Object Drawing.Size(80, 26); $n.Tag = $key
  $n.Add_ValueChanged({ param($s, $e) if (-not $global:loading) { $cfg[$s.Tag] = [int]$s.Value; Apply } })
  $form.Controls.Add($n); $nums[$key] = $n
}
$l = Lbl 'Key hold' 24 400 66 26;  $tip.SetToolTip($l, 'How long each key is held down');          Num 96 400 'Hold' 1
$l = Lbl 'Click sync' 200 400 76 26; $tip.SetToolTip($l, 'Gap between key press and click');       Num 276 400 'Sync' 0
$l = Lbl 'Delay 1' 24 434 66 26;   $tip.SetToolTip($l, 'Time from slot 1 step to slot 2 step');   Num 96 434 'D1' 10
$l = Lbl 'Delay 2' 200 434 76 26;  $tip.SetToolTip($l, 'Time from slot 2 step to next cycle');    Num 276 434 'D2' 10
[void](Lbl 'Minecraft reads input every 50 ms - delays under 50 may drop actions.' 24 466 332 16 $cDim $fSmall)

# ---------- bottom buttons ----------
$btnOn = Pill '' 24 492 140 36
$animOn = $global:anim[$global:anim.Count - 1]
$btnOn.Add_Click({ [M]::On = -not [M]::On; Refresh })
$btnSave = Pill 'SAVE' 172 492 90 36
$btnSave.Add_Click({ Save; $btnSave.Text = 'SAVED'; $btnSave.ForeColor = $cG; $global:savedAt = [DateTime]::Now })
$btnReset = Pill 'RESET' 270 492 86 36
$btnReset.Add_Click({ foreach ($k in $defaults.Keys) { $cfg[$k] = $defaults[$k] }; Apply; Refresh })

$clickNames = @{ L = 'Left click'; R = 'Right click'; N = 'No click' }
function Refresh {
  $global:loading = $true
  foreach ($k in $btns.Keys) { $btns[$k].Text = (KN $cfg[$k]) }
  foreach ($k in $cbtns.Keys) { $cbtns[$k].Text = $clickNames[[string]$cfg[$k]] }
  foreach ($k in $nums.Keys) { $nums[$k].Value = [Math]::Max($nums[$k].Minimum, [Math]::Min(2000, [int]$cfg[$k])) }
  if ([M]::On) {
    $btnOn.Text = 'MACRO ON'; $btnOn.ForeColor = $cG
    $animOn.base = RGB 20 62 46; $animOn.hov = RGB 28 86 62
  } else {
    $btnOn.Text = 'MACRO OFF'; $btnOn.ForeColor = $cR
    $animOn.base = RGB 68 26 36; $animOn.hov = RGB 92 34 46
  }
  $global:loading = $false
}
function StartCap($k) {
  if ($global:cap) { CancelCap }
  $global:cap = $k; $global:wait = $true
  if ($k -eq 'Trig' -or $k -eq 'Tog') { $btns[$k].Text = 'Press key / mouse...' } else { $btns[$k].Text = 'Press a key...' }
}
function CancelCap {
  $global:cap = $null; Refresh
}

# ---------- painting ----------
$form.Add_Paint({
  param($s, $e)
  try {
  $g = $e.Graphics; $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
  $W = $form.ClientSize.Width
  $t = ([math]::Sin($global:ph) + 1) / 2
  $c1 = Mix $cA $cB $t; $c2 = Mix $cB $cC (1 - $t)

  # animated top accent bar
  $r = New-Object Drawing.Rectangle(0, 0, $W, 3)
  $br = New-Object Drawing.Drawing2D.LinearGradientBrush($r, $c1, $c2, [single]0)
  $g.FillRectangle($br, $r); $br.Dispose()
  if ($global:state -eq 2) {
    $sx = (($global:ph * 150) % ($W + 120)) - 120
    $g.FillRectangle($shine, [int]$sx, 0, 120, 3)
  }

  # animated gradient title
  $tr = New-Object Drawing.Rectangle(20, 12, 170, 30)
  $tb = New-Object Drawing.Drawing2D.LinearGradientBrush($tr, $c1, $c2, [single]0)
  $g.DrawString('ROD MACRO', $fT, $tb, 22, 12); $tb.Dispose()
  $g.DrawString('Sword / Rod combo', $fSmall, $dimBrush, 24, 42)

  # status orb with ripples
  $dc = $global:stCol
  if ($global:state -ge 1) {
    for ($k = 0; $k -lt 2; $k++) {
      $f = ($global:rp + $k * 0.5) % 1
      $rad = 9 + 17 * $f
      $rc = [Drawing.Color]::FromArgb([int](110 * (1 - $f)), $dc)
      $pen = New-Object Drawing.Pen -ArgumentList $rc, ([single]1.5)
      $g.DrawEllipse($pen, [single](44 - $rad), [single](82 - $rad), [single]($rad * 2), [single]($rad * 2)); $pen.Dispose()
    }
  }
  $core = 8 + 0.8 * [math]::Sin($global:ph * 3)
  $gb = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(60, $dc))
  $g.FillEllipse($gb, [single](44 - $core - 4), [single](82 - $core - 4), [single](($core + 4) * 2), [single](($core + 4) * 2)); $gb.Dispose()
  $cb = New-Object Drawing.SolidBrush $dc
  $g.FillEllipse($cb, [single](44 - $core), [single](82 - $core), [single]($core * 2), [single]($core * 2)); $cb.Dispose()
  $sb = New-Object Drawing.SolidBrush $dc
  $g.DrawString($global:stTxt, $fBig, $sb, 78, 64); $sb.Dispose()
  $g.DrawString($global:hint, $fSmall, $dimBrush, 80, 89)

  $g.FillRectangle($lineBrush, 24, 112, ($W - 48), 1)
  } catch { LogErr $_.Exception.ToString() }
})
$form.Add_MouseDown({ param($s, $e) if ($e.Button -eq 'Left' -and $e.Y -lt 112) { [M]::Drag($form.Handle) } })

# ---------- animation / polling timer ----------
$timer = New-Object Windows.Forms.Timer
$timer.Interval = 30
$timer.Add_Tick({
 try {
  $global:ph += 0.06
  $global:rp = ($global:rp + $(if ($global:state -eq 2) { 0.03 } else { 0.011 })) % 1000

  # button hover fade
  $mp = $form.PointToClient([Windows.Forms.Cursor]::Position)
  foreach ($it in $global:anim) {
    $b = $it.b
    if ($global:cap -and $b.Tag -eq $global:cap) { continue }
    $target = 0.0; if ($b.Bounds.Contains($mp)) { $target = 1.0 }
    $it.h += ($target - $it.h) * 0.28
    $c = Mix ($it.base) ($it.hov) ($it.h)
    if ($b.BackColor.ToArgb() -ne $c.ToArgb()) {
      $b.BackColor = $c; $b.FlatAppearance.MouseOverBackColor = $c; $b.FlatAppearance.MouseDownBackColor = $c
    }
  }

  # key capture / toggle key
  if ($global:cap) {
    $b = $btns[$global:cap]
    $c = Mix $cPanel $cB (([math]::Sin($global:ph * 4) + 1) / 2 * 0.8)
    $b.BackColor = $c; $b.FlatAppearance.MouseOverBackColor = $c; $b.FlatAppearance.MouseDownBackColor = $c
    $k = [M]::FirstDown(($global:cap -eq 'Trig' -or $global:cap -eq 'Tog'))
    if ($global:wait) { if ($k -eq 0) { $global:wait = $false } }
    elseif ($k -ne 0) {
      if ($k -eq 27) { CancelCap }
      else { $cfg[$global:cap] = $k; $global:cap = $null; Apply; Refresh }
    }
    $global:togPrev = $true
  } else {
    $now = [M]::Down([int]$cfg.Tog)
    if ($now -and -not $global:togPrev) { [M]::On = -not [M]::On; Refresh }
    $global:togPrev = $now
  }
  if ($global:savedAt -and ([DateTime]::Now - $global:savedAt).TotalSeconds -gt 1.2) { $btnSave.Text = 'SAVE'; $btnSave.ForeColor = $cTxt; $global:savedAt = $null }

  # status
  if (-not [M]::On) { $global:state = 0; $global:stTxt = 'OFF'; $global:stCol = $cR; $global:hint = 'Press ' + (KN $cfg.Tog) + ' to enable' }
  elseif ([M]::Active) { $global:state = 2; $global:stTxt = 'RUNNING'; $global:stCol = $cG; $global:hint = 'Combo active' }
  else { $global:state = 1; $global:stTxt = 'READY'; $global:stCol = $cY; $global:hint = 'Hold ' + (KN $cfg.Trig) + ' to run' }
  if ($cfg.Trig -eq $cfg.K1 -or $cfg.Trig -eq $cfg.K2) { $global:stCol = RGB 255 140 60; $global:hint = 'Warning: trigger equals a slot key' }
  $form.Invalidate((New-Object Drawing.Rectangle(0, 0, 380, 114)))
 } catch { LogErr $_.Exception.ToString() }
})

$form.Add_FormClosing({ [M]::On = $false; Save; Start-Sleep -Milliseconds 60 })

$form.Add_Shown({ $form.Activate(); $form.BringToFront() })
Apply
Refresh
$ErrorActionPreference = 'Continue'
[M]::Start()
$timer.Start()
[Windows.Forms.Application]::Run($form)
} catch {
  [Windows.Forms.MessageBox]::Show($_.Exception.ToString(), 'Rod Macro error') | Out-Null
}
