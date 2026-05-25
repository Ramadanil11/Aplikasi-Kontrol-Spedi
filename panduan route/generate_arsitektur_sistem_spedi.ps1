Add-Type -AssemblyName System.Drawing

$outPath = Join-Path $PSScriptRoot "arsitektur sistem.png"
$w = 2200
$h = 1320
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

function C($hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }
function B($hex) { New-Object System.Drawing.SolidBrush (C $hex) }
function P($hex, $width = 2) { New-Object System.Drawing.Pen (C $hex), $width }
function BA($hex, $alpha) {
    $c = C $hex
    New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($alpha, $c.R, $c.G, $c.B))
}

function TextCenter($text, $x, $y, $ww, $hh, $font, $brush) {
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = [System.Drawing.StringAlignment]::Center
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = New-Object System.Drawing.RectangleF $x, $y, $ww, $hh
    $g.DrawString($text, $font, $brush, $rect, $sf)
}

function TextLeft($text, $x, $y, $ww, $hh, $font, $brush) {
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = [System.Drawing.StringAlignment]::Near
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = New-Object System.Drawing.RectangleF $x, $y, $ww, $hh
    $g.DrawString($text, $font, $brush, $rect, $sf)
}

function RoundPath($x, $y, $ww, $hh, $r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x, $y, $r, $r, 180, 90)
    $path.AddArc($x + $ww - $r, $y, $r, $r, 270, 90)
    $path.AddArc($x + $ww - $r, $y + $hh - $r, $r, $r, 0, 90)
    $path.AddArc($x, $y + $hh - $r, $r, $r, 90, 90)
    $path.CloseFigure()
    return $path
}

function Section($x, $y, $ww, $hh, $title, $fill) {
    $path = RoundPath $x $y $ww $hh 22
    $g.FillPath((B $fill), $path)
    $g.DrawPath((P "#1e293b" 2.5), $path)
    TextCenter $title $x ($y + 18) $ww 40 $script:SectionFont (B "#0f172a")
}

function Card($x, $y, $ww, $hh, $title, $body, $fill, $stroke) {
    $shadow = RoundPath ($x + 6) ($y + 8) $ww $hh 14
    $g.FillPath((BA "#0f172a" 18), $shadow)
    $path = RoundPath $x $y $ww $hh 14
    $g.FillPath((B $fill), $path)
    $g.DrawPath((P $stroke 2.7), $path)
    TextCenter $title $x ($y + 10) $ww 30 $script:CardTitleFont (B "#0f172a")
    TextCenter $body ($x + 18) ($y + 44) ($ww - 36) ($hh - 50) $script:CardBodyFont (B "#334155")
}

function SmallCard($x, $y, $ww, $hh, $title, $body, $fill, $stroke) {
    $path = RoundPath $x $y $ww $hh 12
    $g.FillPath((B $fill), $path)
    $g.DrawPath((P $stroke 2.2), $path)
    TextLeft $title ($x + 16) ($y + 8) ($ww - 28) 24 $script:SmallTitleFont (B "#0f172a")
    TextLeft $body ($x + 16) ($y + 34) ($ww - 28) ($hh - 38) $script:SmallBodyFont (B "#475569")
}

function Arrow($x1, $y1, $x2, $y2, $label, $color = "#334155", $dash = $false) {
    $pen = P $color 3
    if ($dash) { $pen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash }
    $pen.CustomEndCap = New-Object System.Drawing.Drawing2D.AdjustableArrowCap 7, 8
    $g.DrawLine($pen, $x1, $y1, $x2, $y2)
    if ($label -ne "") {
        $lx = [Math]::Min($x1, $x2) + [Math]::Abs($x2 - $x1) / 2 - 145
        $ly = [Math]::Min($y1, $y2) + [Math]::Abs($y2 - $y1) / 2 - 18
        TextCenter $label $lx $ly 290 34 $script:ArrowFont (B $color)
    }
}

function PolyArrow($points, $label, $labelX, $labelY, $color = "#334155", $dash = $false) {
    $base = P $color 3
    $end = P $color 3
    if ($dash) {
        $base.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
        $end.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
    }
    $end.CustomEndCap = New-Object System.Drawing.Drawing2D.AdjustableArrowCap 7, 8
    for ($i = 0; $i -lt $points.Count - 1; $i++) {
        $pen = if ($i -eq $points.Count - 2) { $end } else { $base }
        $g.DrawLine($pen, $points[$i][0], $points[$i][1], $points[$i + 1][0], $points[$i + 1][1])
    }
    if ($label -ne "") { TextCenter $label $labelX $labelY 300 34 $script:ArrowFont (B $color) }
}

function DrawBoat($cx, $cy) {
    $hull = @(
        (New-Object System.Drawing.PointF $cx, ($cy - 112)),
        (New-Object System.Drawing.PointF ($cx + 145), ($cy + 8)),
        (New-Object System.Drawing.PointF ($cx + 102), ($cy + 122)),
        (New-Object System.Drawing.PointF ($cx - 102), ($cy + 122)),
        (New-Object System.Drawing.PointF ($cx - 145), ($cy + 8))
    )
    $deck = @(
        (New-Object System.Drawing.PointF $cx, ($cy - 62)),
        (New-Object System.Drawing.PointF ($cx + 88), ($cy + 14)),
        (New-Object System.Drawing.PointF ($cx + 60), ($cy + 84)),
        (New-Object System.Drawing.PointF ($cx - 60), ($cy + 84)),
        (New-Object System.Drawing.PointF ($cx - 88), ($cy + 14))
    )
    $g.FillPolygon((B "#0f766e"), $hull)
    $g.DrawPolygon((P "#134e4a" 5), $hull)
    $g.FillPolygon((B "#67e8f9"), $deck)
    $g.DrawPolygon((P "#0891b2" 3), $deck)
    $g.FillEllipse((B "#fef3c7"), $cx - 74, $cy - 4, 52, 32)
    $g.FillEllipse((B "#fef3c7"), $cx + 22, $cy - 4, 52, 32)
    $g.DrawEllipse((P "#d97706" 2.4), $cx - 74, $cy - 4, 52, 32)
    $g.DrawEllipse((P "#d97706" 2.4), $cx + 22, $cy - 4, 52, 32)
}

function DrawDatabase($cx, $cy) {
    $pen = P "#16a34a" 4
    for ($i = 0; $i -lt 3; $i++) {
        $yy = $cy + ($i * 40)
        $g.FillEllipse((B "#dcfce7"), $cx - 92, $yy - 23, 184, 46)
        $g.DrawEllipse($pen, $cx - 92, $yy - 23, 184, 46)
        if ($i -lt 2) {
            $g.FillRectangle((B "#dcfce7"), $cx - 92, $yy, 184, 40)
            $g.DrawLine($pen, $cx - 92, $yy, $cx - 92, $yy + 40)
            $g.DrawLine($pen, $cx + 92, $yy, $cx + 92, $yy + 40)
        }
    }
}

function DrawPhone($x, $y, $ww, $hh) {
    $body = RoundPath $x $y $ww $hh 28
    $g.FillPath((B "#111827"), $body)
    $screen = RoundPath ($x + 18) ($y + 28) ($ww - 36) ($hh - 62) 18
    $g.FillPath((B "#f8fafc"), $screen)
    $g.DrawPath((P "#334155" 2), $screen)
    $g.FillEllipse((B "#94a3b8"), ($x + $ww / 2 - 8), ($y + $hh - 27), 16, 16)
    SmallCard ($x + 42) ($y + 70) ($ww - 84) 68 "MANUAL" "joystick & emergency stop" "#dbeafe" "#2563eb"
    SmallCard ($x + 42) ($y + 158) ($ww - 84) 68 "GRID / ROUTE" "waypoint, execute, stop" "#e0f2fe" "#0284c7"
    SmallCard ($x + 42) ($y + 246) ($ww - 84) 68 "TELEMETRY" "GPS, speed, heading" "#f0fdfa" "#0f766e"
}

function DrawActor($cx, $cy) {
    $pen = P "#0f172a" 4
    $g.FillEllipse((B "#dbeafe"), $cx - 28, $cy - 82, 56, 56)
    $g.DrawEllipse($pen, $cx - 28, $cy - 82, 56, 56)
    $g.DrawLine($pen, $cx, $cy - 26, $cx, $cy + 58)
    $g.DrawLine($pen, $cx - 52, $cy + 8, $cx + 52, $cy + 8)
    $g.DrawLine($pen, $cx, $cy + 58, $cx - 48, $cy + 126)
    $g.DrawLine($pen, $cx, $cy + 58, $cx + 48, $cy + 126)
}

$TitleFont = New-Object System.Drawing.Font "Arial", 34, ([System.Drawing.FontStyle]::Bold)
$SubtitleFont = New-Object System.Drawing.Font "Arial", 15, ([System.Drawing.FontStyle]::Regular)
$SectionFont = New-Object System.Drawing.Font "Arial", 21, ([System.Drawing.FontStyle]::Bold)
$CardTitleFont = New-Object System.Drawing.Font "Arial", 15, ([System.Drawing.FontStyle]::Bold)
$CardBodyFont = New-Object System.Drawing.Font "Arial", 12, ([System.Drawing.FontStyle]::Regular)
$SmallTitleFont = New-Object System.Drawing.Font "Arial", 11.5, ([System.Drawing.FontStyle]::Bold)
$SmallBodyFont = New-Object System.Drawing.Font "Arial", 10.2, ([System.Drawing.FontStyle]::Regular)
$ArrowFont = New-Object System.Drawing.Font "Arial", 10.5, ([System.Drawing.FontStyle]::Bold)
$NoteFont = New-Object System.Drawing.Font "Arial", 11.5, ([System.Drawing.FontStyle]::Regular)

$g.Clear((C "#ffffff"))
TextCenter "ARSITEKTUR SISTEM SPEDI" 0 26 $w 58 $TitleFont (B "#020617")
TextCenter "Aplikasi kontrol kapal berbasis Flutter, Backend API, MQTT, WebSocket, dan ESP32" 0 82 $w 32 $SubtitleFont (B "#475569")

Section 55 138 455 1055 "KAPAL SPEDI / DEVICE" "#ecfeff"
Section 545 138 455 1055 "KOMUNIKASI" "#f8fafc"
Section 1035 138 455 1055 "BACKEND & DATABASE" "#f0fdf4"
Section 1525 138 620 1055 "APLIKASI SPEDI" "#eff6ff"

# Device.
DrawBoat 280 335
Card 100 500 365 120 "ESP32 Firmware" "WiFi + PubSubClient`nsubscribe command MQTT`npublish telemetry status" "#ecfeff" "#0f766e"
SmallCard 100 650 365 80 "Sensor navigasi" "GPS u-blox NEO-M8U, satellite, HDOP, heading, obstacle ultrasonic" "#f0fdfa" "#0f766e"
SmallCard 100 755 365 80 "Aktuator" "Motor driver, propeller, servo kemudi" "#fef3c7" "#d97706"
SmallCard 100 860 365 110 "Autopilot & safety" "Route otomatis mengikuti waypoint. Emergency stop mengembalikan throttle 0 dan steering 0." "#fee2e2" "#dc2626"
SmallCard 100 1000 365 86 "Syarat route" "GPS lock wajib. Route ditolak jika waypoint kurang dari 2 atau GPS belum lock." "#fff7ed" "#ea580c"

# Communication.
Card 590 240 365 120 "MQTT Broker" "Jalur komunikasi device real-time untuk command dan telemetry" "#dcfce7" "#16a34a"
SmallCard 590 405 365 80 "spedi/vehicle/joystick" "Aplikasi mengirim throttle, steering, dan emergency stop." "#dbeafe" "#2563eb"
SmallCard 590 510 365 80 "spedi/vehicle/route" "Aplikasi mengirim start, stop, dan daftar waypoint." "#e0f2fe" "#0284c7"
SmallCard 590 615 365 90 "spedi/vehicle/status" "ESP32 publish GPS, speed, heading, obstacle, dan route_event." "#f0fdfa" "#0f766e"
Card 590 765 365 115 "WebSocket" "Kontrol real-time aplikasi melalui backend." "#ede9fe" "#7c3aed"
Card 590 930 365 115 "REST API" "Auth, session device, route, waypoint, dan telemetry database." "#fef3c7" "#d97706"

# Backend.
DrawDatabase 1265 245
Card 1080 380 365 115 "Backend SPEDI" "API server untuk autentikasi, session, route, telemetry, dan integrasi MQTT/WebSocket." "#dcfce7" "#16a34a"
SmallCard 1080 530 365 78 "Auth & session" "Login/register, token, device aktif." "#dcfce7" "#16a34a"
SmallCard 1080 630 365 86 "Route API" "Simpan route, waypoint, start/stop route, dan status route." "#e0f2fe" "#0284c7"
SmallCard 1080 740 365 86 "Telemetry database" "Menyimpan raw telemetry dan menyediakan data monitoring." "#f0fdfa" "#0f766e"
SmallCard 1080 850 365 92 "Validasi & notifikasi" "Status device, route gagal, GPS belum lock, route selesai." "#fef3c7" "#d97706"

# Application.
DrawPhone 1610 225 295 470
DrawActor 2050 400
TextCenter "Operator" 1975 548 150 32 $CardTitleFont (B "#0f172a")
Card 1580 735 510 105 "Flutter Android App" "Mode landscape untuk manual control, grid/route, telemetry, dan notifikasi." "#dbeafe" "#2563eb"
SmallCard 1580 875 510 78 "Manual control" "Joystick mengirim throttle dan steering. Emergency stop dapat dipakai kapan saja." "#dbeafe" "#2563eb"
SmallCard 1580 975 510 86 "Grid / route control" "Tambah waypoint di peta. Execute hanya berjalan setelah validasi dan GPS lock." "#e0f2fe" "#0284c7"
SmallCard 1580 1085 510 72 "Monitoring telemetry" "Menampilkan GPS, speed, heading, obstacle, route_event, dan status koneksi." "#f0fdfa" "#0f766e"

# Clear horizontal architecture relationships.
Arrow 465 560 590 280 "connect MQTT" "#0f766e"
Arrow 590 448 465 560 "manual command" "#2563eb"
Arrow 590 553 465 560 "route command" "#0284c7"
Arrow 465 662 590 660 "telemetry status" "#0f766e"

Arrow 955 300 1080 430 "integrasi MQTT" "#16a34a"
Arrow 1445 430 1580 790 "data & status" "#16a34a"
PolyArrow @(@(1580, 914), @(1515, 914), @(1515, 1170), @(1015, 1170), @(1015, 448), @(955, 448)) "joystick" 1125 1138 "#2563eb"
PolyArrow @(@(1580, 1018), @(1535, 1018), @(1535, 1210), @(995, 1210), @(995, 553), @(955, 553)) "route" 1135 1180 "#0284c7"
Arrow 955 660 1580 1120 "telemetry" "#0f766e"

Arrow 1580 795 955 822 "WebSocket real-time" "#7c3aed"
Arrow 1580 1018 955 988 "REST API route" "#d97706"
Arrow 955 988 1080 673 "simpan route" "#d97706"
Arrow 955 660 1080 783 "simpan telemetry" "#0f766e"
Arrow 1445 895 1580 790 "notifikasi" "#d97706" $true
Arrow 1905 405 1990 405 "operasikan" "#334155"

# Route validation emphasis.
PolyArrow @(@(2090, 1018), @(2125, 1018), @(2125, 1240), @(280, 1240), @(280, 1086)) "GPS lock wajib sebelum route jalan" 760 1212 "#ea580c" $true

$bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Output $outPath
