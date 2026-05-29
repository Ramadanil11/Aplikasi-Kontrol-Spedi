Add-Type -AssemblyName System.Drawing

$canvasW = 1800
$canvasH = 1750

$outMainPath = Join-Path $PSScriptRoot "cara_kerja_spedi_flowchart_rth.png"
$outNamedPath = Join-Path $PSScriptRoot "cara_kerja_spedi_RTH.png"
$flowchartSpediPath = Join-Path $PSScriptRoot "flowchart_spedi.png"
$svgPath = Join-Path $PSScriptRoot "cara_kerja_spedi_flowchart_rth.svg"

$bmp = New-Object System.Drawing.Bitmap $canvasW, $canvasH
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

function New-Brush($hex) {
    New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml($hex))
}

function New-Pen($hex, $width) {
    New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml($hex)), $width
}

function Draw-CenteredText($text, $x, $y, $w, $h, $font, $brush) {
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = [System.Drawing.StringAlignment]::Center
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
    $sf.Trimming = [System.Drawing.StringTrimming]::Word
    $sf.FormatFlags = 0
    $rect = New-Object System.Drawing.RectangleF $x, $y, $w, $h
    $g.DrawString($text, $font, $brush, $rect, $sf)
}

function Draw-Node($x, $y, $w, $h, $fill, $stroke, $lines, $font = $script:BodyFont) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 12
    $path.AddArc($x, $y, $r, $r, 180, 90)
    $path.AddArc($x + $w - $r, $y, $r, $r, 270, 90)
    $path.AddArc($x + $w - $r, $y + $h - $r, $r, $r, 0, 90)
    $path.AddArc($x, $y + $h - $r, $r, $r, 90, 90)
    $path.CloseFigure()
    $g.FillPath((New-Brush $fill), $path)
    $g.DrawPath((New-Pen $stroke 2), $path)
    Draw-CenteredText ($lines -join "`n") $x $y $w $h $font (New-Brush "#0f172a")
}

function Draw-Diamond($cx, $cy, $w, $h, $lines, $font = $script:BodyFont) {
    $pts = @(
        (New-Object System.Drawing.Point ([int]$cx), ([int]($cy - $h / 2))),
        (New-Object System.Drawing.Point ([int]($cx + $w / 2)), ([int]$cy)),
        (New-Object System.Drawing.Point ([int]$cx), ([int]($cy + $h / 2))),
        (New-Object System.Drawing.Point ([int]($cx - $w / 2)), ([int]$cy))
    )
    $g.FillPolygon((New-Brush "#fff7ed"), $pts)
    $g.DrawPolygon((New-Pen "#ea580c" 2), $pts)
    Draw-CenteredText ($lines -join "`n") ($cx - $w / 2) ($cy - $h / 2) $w $h $font (New-Brush "#0f172a")
}

function Draw-Arrow($points, $dash = $false) {
    $basePen = New-Pen "#334155" 2.2
    $endPen = New-Pen "#334155" 2.2
    if ($dash) {
        $basePen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
        $endPen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
    }
    $arrowCap = New-Object System.Drawing.Drawing2D.AdjustableArrowCap 7, 8
    $arrowCap.Filled = $true
    $endPen.CustomEndCap = $arrowCap
    for ($i = 0; $i -lt $points.Count - 1; $i++) {
        $pen = if ($i -eq $points.Count - 2) { $endPen } else { $basePen }
        $g.DrawLine($pen, $points[$i][0], $points[$i][1], $points[$i + 1][0], $points[$i + 1][1])
    }
}

function Draw-Label($text, $x, $y) {
    $size = $g.MeasureString($text, $script:LabelFont)
    $bg = New-Object System.Drawing.RectangleF ($x - 6), ($y - 4), ($size.Width + 12), ($size.Height + 6)
    $g.FillRectangle((New-Brush "#ffffff"), $bg)
    $g.DrawString($text, $script:LabelFont, (New-Brush "#334155"), $x, $y)
}

$TitleFont = New-Object System.Drawing.Font "Arial", 21, ([System.Drawing.FontStyle]::Bold)
$SubFont = New-Object System.Drawing.Font "Arial", 10.5, ([System.Drawing.FontStyle]::Regular)
$LaneFont = New-Object System.Drawing.Font "Arial", 13, ([System.Drawing.FontStyle]::Bold)
$BodyFont = New-Object System.Drawing.Font "Arial", 10.5, ([System.Drawing.FontStyle]::Regular)
$SmallFont = New-Object System.Drawing.Font "Arial", 9.5, ([System.Drawing.FontStyle]::Regular)
$LabelFont = New-Object System.Drawing.Font "Arial", 9, ([System.Drawing.FontStyle]::Regular)

$g.Clear([System.Drawing.ColorTranslator]::FromHtml("#f8fafc"))
$g.DrawString("Cara Kerja SPEDI RTH - Firmware v15.0 Cargo", $TitleFont, (New-Brush "#0f172a"), 60, 24)
$g.DrawString("Update dari firmware C:\spedi_boat_v15_cargo: RTH, cargo profile, GPS M8U/UDR, route soft-start, fuzzy control, avoidance, dan telemetry MQTT.", $SubFont, (New-Brush "#475569"), 60, 58)

$panelX = 40
$panelY = 95
$panelW = 1720
$panelH = 1620
$g.FillRectangle((New-Brush "#ffffff"), $panelX, $panelY, $panelW, $panelH)
$g.DrawRectangle((New-Pen "#1e293b" 2), $panelX, $panelY, $panelW, $panelH)
$lanePen = New-Pen "#cbd5e1" 1.4
$g.DrawLine($lanePen, 40, 145, 1760, 145)
$g.DrawLine($lanePen, 390, 95, 390, 1715)
$g.DrawLine($lanePen, 740, 95, 740, 1715)
$g.DrawLine($lanePen, 1280, 95, 1280, 1715)

Draw-CenteredText "Pengguna / Aplikasi Flutter" 40 111 350 30 $LaneFont (New-Brush "#0f172a")
Draw-CenteredText "Backend / API / MQTT" 390 111 350 30 $LaneFont (New-Brush "#0f172a")
Draw-CenteredText "Firmware Kapal ESP32-S3" 740 111 540 30 $LaneFont (New-Brush "#0f172a")
Draw-CenteredText "Monitoring Aplikasi" 1280 111 480 30 $LaneFont (New-Brush "#0f172a")

# Connection and firmware boot
Draw-Node 90 170 260 58 "#e0f2fe" "#0284c7" @("Aplikasi dibuka", "auto-login")
Draw-Node 445 170 250 58 "#dcfce7" "#16a34a" @("Buka session", "device SPEDI")
Draw-Node 445 255 250 58 "#dcfce7" "#16a34a" @("Connect WebSocket", "MQTT + telemetry")
Draw-Node 815 170 390 70 "#fef3c7" "#d97706" @("ESP32 boot", "init motor, servo, sonar, buzzer", "GPS M8U 9600 -> 38400")
Draw-Node 815 270 390 72 "#fef3c7" "#d97706" @("GPS lock FSM", "cache posisi + drift filter", "ESF/UDR aktif setelah lock")
Draw-Diamond 570 410 220 110 @("Koneksi", "siap?")
Draw-Node 825 380 300 58 "#fee2e2" "#dc2626" @("Tampilkan error", "koneksi")
Draw-Diamond 215 410 220 110 @("Operator", "pilih mode?")

# Manual mode
Draw-Node 90 530 260 58 "#e0f2fe" "#0284c7" @("Mode MANUAL")
Draw-Node 445 530 250 58 "#dcfce7" "#16a34a" @("Kirim joystick", "throttle / steering")
Draw-Node 805 520 420 75 "#fef3c7" "#d97706" @("handleJoystick()", "manual override batalkan AUTO/RTH", "normalisasi throttle + steering")
Draw-Node 805 625 420 82 "#fef3c7" "#d97706" @("Gerak cargo halus", "servo smoothing alpha 0.10", "motor ramp 3 step / 40 ms", "timeout joystick 4s -> stop")
Draw-Node 1330 600 330 78 "#f0fdfa" "#0f766e" @("Status realtime", "speed, heading, GPS, WiFi/MQTT", "profile=cargo")

# Route mode
Draw-Node 90 820 260 58 "#e0f2fe" "#0284c7" @("Mode GRID / ROUTE")
Draw-Node 445 820 250 58 "#dcfce7" "#16a34a" @("Kirim MQTT route", "action=start + waypoints")
Draw-Diamond 1015 870 300 122 @("Firmware validasi", "GPS lock, array WP,", "koordinat valid, WP >= 2?")
Draw-Node 1320 805 350 78 "#fee2e2" "#dc2626" @("route_reject", "gps_not_locked / invalid_waypoints", "invalid_coordinate / WP < 2")
Draw-Node 805 975 420 78 "#fef3c7" "#d97706" @("Route diterima", "simpan home point", "mode AUTO + route_start")
Draw-Node 805 1085 420 78 "#fef3c7" "#d97706" @("Route soft-start", "cap 45 PWM lalu naik halus", "selama 12 detik")
Draw-Node 805 1195 420 88 "#fef3c7" "#d97706" @("Autopilot waypoint", "PI steering + heading deadzone", "fuzzy speed dari obstacle + error heading", "avoidance reverse / turn / recover")
Draw-Node 1320 1000 350 92 "#f0fdfa" "#0f766e" @("Event route di UI", "route_start, wp_reached", "route_complete, route_stop", "route_reject + reason")
Draw-Node 1320 1175 350 88 "#f0fdfa" "#0f766e" @("Telemetry navigasi", "waypoint index/count, dist", "sonar L/R, fuzzy output", "servo, motor, GPS quality")

# RTH and failsafe
Draw-Node 90 1040 260 58 "#e0f2fe" "#0284c7" @("Tombol RTH", "Return Home")
Draw-Node 445 1040 250 58 "#dcfce7" "#16a34a" @("Kirim MQTT route", "action=rth / return_home")
Draw-Diamond 1015 1345 280 112 @("WiFi hilang", "> 5 detik saat", "MODE_AUTO?")
Draw-Node 1165 1330 115 50 "#fef3c7" "#d97706" @("Lanjut", "autopilot") $SmallFont
Draw-Diamond 1015 1485 250 112 @("Home point", "tersedia?")
Draw-Node 760 1570 210 62 "#fee2e2" "#dc2626" @("RTH ditolak", "rth_reject", "home_not_set") $SmallFont
Draw-Node 1045 1570 210 62 "#ede9fe" "#7c3aed" @("startReturnToHome()", "waypoint home", "mode RTH") $SmallFont
Draw-Node 1320 1435 350 86 "#f0fdfa" "#0f766e" @("Telemetry RTH", "mode=rth, rth_active", "home_set, home_lat/lng", "route_reason=wifi_lost/manual")
Draw-Node 1320 1550 350 62 "#ede9fe" "#7c3aed" @("Home tercapai", "rth_complete + stop")

# Emergency stop
Draw-Node 90 1560 260 58 "#fee2e2" "#dc2626" @("Emergency Stop")
Draw-Node 445 1560 250 58 "#fee2e2" "#dc2626" @("Kirim command stop")
Draw-Node 805 1648 420 42 "#fee2e2" "#dc2626" @("Throttle 0, steering center, reset avoidance, mode IDLE") $SmallFont

# Main arrows
Draw-Arrow @(@(350,199), @(445,199))
Draw-Arrow @(@(570,228), @(570,255))
Draw-Arrow @(@(570,313), @(570,355))
Draw-Arrow @(@(695,284), @(815,284))
Draw-Arrow @(@(460,410), @(325,410))
Draw-Label "Ya" 365 390
Draw-Arrow @(@(680,410), @(825,410))
Draw-Label "Tidak" 715 385

Draw-Arrow @(@(215,465), @(215,530))
Draw-Arrow @(@(105,410), @(60,410), @(60,849), @(90,849))
Draw-Arrow @(@(105,410), @(50,410), @(50,1069), @(90,1069))
Draw-Arrow @(@(105,410), @(45,410), @(45,1589), @(90,1589))

Draw-Arrow @(@(350,559), @(445,559))
Draw-Arrow @(@(695,559), @(805,559))
Draw-Arrow @(@(1015,595), @(1015,625))
Draw-Arrow @(@(1225,666), @(1330,640))
Draw-Label "status MQTT" 1245 615
Draw-Arrow @(@(350,849), @(445,849))
Draw-Arrow @(@(695,849), @(865,870))
Draw-Arrow @(@(1165,870), @(1320,844))
Draw-Label "Tidak" 1230 835
Draw-Arrow @(@(1015,931), @(1015,975))
Draw-Label "Ya" 1048 943
Draw-Arrow @(@(1015,1053), @(1015,1085))
Draw-Arrow @(@(1015,1163), @(1015,1195))
Draw-Arrow @(@(1225,1014), @(1320,1046))
Draw-Label "event" 1250 1014
Draw-Arrow @(@(1225,1239), @(1320,1219))
Draw-Label "telemetry" 1245 1200

Draw-Arrow @(@(350,1069), @(445,1069))
Draw-Arrow @(@(695,1069), @(740,1069), @(740,1485), @(890,1485))
Draw-Label "manual RTH" 620 1112

Draw-Arrow @(@(1015,1283), @(1015,1289)) $true
Draw-Label "monitor failsafe" 1045 1284
Draw-Arrow @(@(1155,1345), @(1165,1355))
Draw-Label "Tidak" 1147 1320
Draw-Arrow @(@(1015,1401), @(1015,1429))
Draw-Label "Ya" 1048 1413
Draw-Arrow @(@(890,1485), @(865,1485), @(865,1570))
Draw-Label "Tidak" 830 1456
Draw-Arrow @(@(1140,1485), @(1150,1485), @(1150,1570))
Draw-Label "Ya" 1165 1456
Draw-Arrow @(@(1255,1601), @(1320,1478))
Draw-Arrow @(@(1255,1601), @(1320,1581))
Draw-Arrow @(@(1495,1612), @(1495,1636), @(1015,1636), @(1015,1648)) $true
Draw-Arrow @(@(865,1632), @(865,1638), @(1015,1638), @(1015,1648)) $true

Draw-Arrow @(@(350,1589), @(445,1589))
Draw-Arrow @(@(695,1589), @(740,1589), @(740,1669), @(805,1669))

$g.DrawString("Catatan: home point dibuat saat route start berhasil dari posisi GPS/filter saat itu. RTH bisa dipicu tombol aplikasi atau otomatis oleh WiFi failsafe saat AUTO; manual joystick membatalkan AUTO/RTH.", $SubFont, (New-Brush "#475569"), 60, 1718)

$bmp.Save($outMainPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Save($outNamedPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Save($flowchartSpediPath, [System.Drawing.Imaging.ImageFormat]::Png)

$base64 = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($outMainPath))
$svg = @"
<svg xmlns="http://www.w3.org/2000/svg" width="$canvasW" height="$canvasH" viewBox="0 0 $canvasW $canvasH">
  <title>Cara Kerja SPEDI RTH - Firmware v15.0 Cargo</title>
  <image href="data:image/png;base64,$base64" x="0" y="0" width="$canvasW" height="$canvasH"/>
</svg>
"@
Set-Content -LiteralPath $svgPath -Value $svg -Encoding UTF8

$g.Dispose()
$bmp.Dispose()
Write-Output $outMainPath
Write-Output $outNamedPath
Write-Output $flowchartSpediPath
Write-Output $svgPath
