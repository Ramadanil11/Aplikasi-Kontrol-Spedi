Add-Type -AssemblyName System.Drawing

$outPath = Join-Path $PSScriptRoot "cara_kerja_spedi_flowchart_rth.png"
$flowchartSpediPath = Join-Path $PSScriptRoot "flowchart_spedi.png"
$bmp = New-Object System.Drawing.Bitmap 1600, 1500
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
    $rect = New-Object System.Drawing.RectangleF $x, $y, $w, $h
    $g.DrawString($text, $font, $brush, $rect, $sf)
}

function Draw-Node($x, $y, $w, $h, $fill, $stroke, $lines) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 12
    $path.AddArc($x, $y, $r, $r, 180, 90)
    $path.AddArc($x + $w - $r, $y, $r, $r, 270, 90)
    $path.AddArc($x + $w - $r, $y + $h - $r, $r, $r, 0, 90)
    $path.AddArc($x, $y + $h - $r, $r, $r, 90, 90)
    $path.CloseFigure()
    $g.FillPath((New-Brush $fill), $path)
    $g.DrawPath((New-Pen $stroke 2), $path)
    Draw-CenteredText ($lines -join "`n") $x $y $w $h $script:BodyFont (New-Brush "#0f172a")
}

function Draw-Diamond($cx, $cy, $w, $h, $lines) {
    $pts = @(
        (New-Object System.Drawing.Point ([int]$cx), ([int]($cy - $h / 2))),
        (New-Object System.Drawing.Point ([int]($cx + $w / 2)), ([int]$cy)),
        (New-Object System.Drawing.Point ([int]$cx), ([int]($cy + $h / 2))),
        (New-Object System.Drawing.Point ([int]($cx - $w / 2)), ([int]$cy))
    )
    $g.FillPolygon((New-Brush "#fff7ed"), $pts)
    $g.DrawPolygon((New-Pen "#ea580c" 2), $pts)
    Draw-CenteredText ($lines -join "`n") ($cx - $w / 2) ($cy - $h / 2) $w $h $script:BodyFont (New-Brush "#0f172a")
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

$TitleFont = New-Object System.Drawing.Font "Arial", 20, ([System.Drawing.FontStyle]::Bold)
$SubFont = New-Object System.Drawing.Font "Arial", 10, ([System.Drawing.FontStyle]::Regular)
$LaneFont = New-Object System.Drawing.Font "Arial", 13, ([System.Drawing.FontStyle]::Bold)
$BodyFont = New-Object System.Drawing.Font "Arial", 11, ([System.Drawing.FontStyle]::Regular)
$SmallFont = New-Object System.Drawing.Font "Arial", 10, ([System.Drawing.FontStyle]::Regular)
$LabelFont = New-Object System.Drawing.Font "Arial", 9, ([System.Drawing.FontStyle]::Regular)

$g.Clear([System.Drawing.ColorTranslator]::FromHtml("#f8fafc"))
$g.DrawString("Cara Kerja SPEDI - Flowchart dengan RTH", $TitleFont, (New-Brush "#0f172a"), 60, 26)
$g.DrawString("Versi baru: menambahkan Return To Home (RTH) dari fitur aplikasi/firmware tanpa menimpa file lama.", $SubFont, (New-Brush "#475569"), 60, 58)

$g.FillRectangle((New-Brush "#ffffff"), 40, 95, 1520, 1340)
$g.DrawRectangle((New-Pen "#1e293b" 2), 40, 95, 1520, 1340)
$lanePen = New-Pen "#cbd5e1" 1.4
$g.DrawLine($lanePen, 40, 145, 1560, 145)
$g.DrawLine($lanePen, 380, 95, 380, 1435)
$g.DrawLine($lanePen, 720, 95, 720, 1435)
$g.DrawLine($lanePen, 1120, 95, 1120, 1435)

Draw-CenteredText "Pengguna / Aplikasi Flutter" 40 111 340 30 $LaneFont (New-Brush "#0f172a")
Draw-CenteredText "Backend / API / MQTT" 380 111 340 30 $LaneFont (New-Brush "#0f172a")
Draw-CenteredText "Firmware Kapal ESP32" 720 111 400 30 $LaneFont (New-Brush "#0f172a")
Draw-CenteredText "Monitoring Aplikasi" 1120 111 440 30 $LaneFont (New-Brush "#0f172a")

Draw-Node 90 170 240 56 "#e0f2fe" "#0284c7" @("Aplikasi dibuka", "dan auto-login")
Draw-Node 430 170 240 56 "#dcfce7" "#16a34a" @("Buka session", "device SPEDI")
Draw-Node 430 255 240 56 "#dcfce7" "#16a34a" @("Connect WebSocket,", "MQTT, telemetry")
Draw-Diamond 550 405 210 110 @("Koneksi", "siap?")
Draw-Node 790 365 240 56 "#fee2e2" "#dc2626" @("Tampilkan error", "koneksi")
Draw-Diamond 210 405 210 110 @("Operator", "pilih mode?")

Draw-Node 90 510 240 56 "#e0f2fe" "#0284c7" @("Mode MANUAL")
Draw-Node 430 510 240 56 "#dcfce7" "#16a34a" @("Kirim joystick", "throttle / steering")
Draw-Node 790 510 260 56 "#fef3c7" "#d97706" @("ESP32 baca", "command joystick")
Draw-Node 790 590 260 56 "#fef3c7" "#d97706" @("Motor dan servo", "bergerak")
Draw-Node 1210 590 260 56 "#f0fdfa" "#0f766e" @("UI update speed,", "heading, GPS")

Draw-Node 90 685 240 56 "#e0f2fe" "#0284c7" @("Mode GRID / ROUTE")
Draw-Diamond 550 713 220 106 @("GPS lock, koordinat,", "waypoint >= 2?")
Draw-Node 430 805 240 56 "#fee2e2" "#dc2626" @("Notif validasi gagal", "GPS / waypoint belum siap")
Draw-Node 790 685 260 56 "#dcfce7" "#16a34a" @("Create route backend", "dan start via MQTT")
Draw-Node 790 780 260 70 "#fef3c7" "#d97706" @("Firmware validasi route", "simpan home point", "mode AUTO + route_start")
Draw-Node 790 890 260 70 "#fef3c7" "#d97706" @("Autopilot ke waypoint", "PI steering, fuzzy speed,", "obstacle avoidance")
Draw-Node 1210 770 260 70 "#f0fdfa" "#0f766e" @("UI tampilkan event", "route_start, wp_reached,", "route_complete/reject")
Draw-Node 1060 1012 150 46 "#fef3c7" "#d97706" @("Lanjut", "autopilot")

Draw-Diamond 920 1035 240 110 @("WiFi hilang", "> 5 detik saat", "MODE_AUTO?")
Draw-Diamond 920 1170 230 110 @("Home point", "tersedia?")
Draw-Node 740 1245 175 62 "#fee2e2" "#dc2626" @("RTH ditolak", "home_not_set")
Draw-Node 935 1245 175 62 "#ede9fe" "#7c3aed" @("Waypoint home", "mode RTH")
Draw-Node 1210 1240 260 70 "#f0fdfa" "#0f766e" @("Telemetry RTH", "mode=rth, rth_active,", "home_set, home_lat/lng")
Draw-Node 1210 1330 260 56 "#ede9fe" "#7c3aed" @("Home tercapai", "rth_complete + stop")

Draw-Node 90 1345 240 56 "#fee2e2" "#dc2626" @("Emergency Stop")
Draw-Node 430 1345 240 56 "#fee2e2" "#dc2626" @("Kirim command stop")
Draw-Node 790 1365 260 36 "#fee2e2" "#dc2626" @("Throttle 0, steering center, mode IDLE")

Draw-Arrow @(@(330,198), @(430,198))
Draw-Arrow @(@(550,226), @(550,255))
Draw-Arrow @(@(550,311), @(550,350))
Draw-Arrow @(@(445,405), @(315,405))
Draw-Label "Ya" 365 385
Draw-Arrow @(@(655,405), @(790,393))
Draw-Label "Tidak" 706 379

Draw-Arrow @(@(210,460), @(210,510))
Draw-Arrow @(@(105,405), @(60,405), @(60,713), @(90,713))
Draw-Arrow @(@(105,405), @(45,405), @(45,1373), @(90,1373))

Draw-Arrow @(@(330,538), @(430,538))
Draw-Arrow @(@(670,538), @(790,538))
Draw-Arrow @(@(920,566), @(920,590))
Draw-Arrow @(@(1050,618), @(1210,618))
Draw-Label "status MQTT" 1086 590

Draw-Arrow @(@(330,713), @(440,713))
Draw-Arrow @(@(660,713), @(790,713))
Draw-Label "Ya" 692 694
Draw-Arrow @(@(550,766), @(550,805))
Draw-Label "Tidak" 585 784
Draw-Arrow @(@(920,741), @(920,780))
Draw-Arrow @(@(1050,815), @(1210,805))
Draw-Label "event" 1088 777
Draw-Arrow @(@(920,850), @(920,890))

Draw-Arrow @(@(920,960), @(920,980)) $true
Draw-Label "monitor failsafe" 954 961
Draw-Arrow @(@(1040,1035), @(1060,1035)) $true
Draw-Label "Tidak" 1054 1008
Draw-Arrow @(@(920,1090), @(920,1115))
Draw-Label "Ya" 951 1102
Draw-Arrow @(@(805,1170), @(827,1170), @(827,1245))
Draw-Label "Tidak" 766 1142
Draw-Arrow @(@(1035,1170), @(1022,1170), @(1022,1245))
Draw-Label "Ya" 1055 1142
Draw-Arrow @(@(1110,1276), @(1210,1276))
Draw-Arrow @(@(1022,1307), @(1022,1358), @(1210,1358))
Draw-Arrow @(@(827,1307), @(827,1340), @(920,1340), @(920,1365)) $true
Draw-Arrow @(@(1340,1386), @(1340,1420), @(920,1420), @(920,1401)) $true

Draw-Arrow @(@(330,1373), @(430,1373))
Draw-Arrow @(@(670,1373), @(725,1373), @(725,1383), @(790,1383))

$g.DrawString("Catatan: home point dibuat saat route start berhasil. RTH otomatis aktif saat WiFi hilang ketika mode AUTO; jika home belum tersedia, firmware mengirim rth_reject.", $SubFont, (New-Brush "#475569"), 60, 1465)

$bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Save($flowchartSpediPath, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Output $outPath
Write-Output $flowchartSpediPath
