import 'dart:async';

import 'package:flutter/foundation.dart';

import '../core/api_client.dart';

class DatabaseTelemetryService {
  DatabaseTelemetryService._();
  static final DatabaseTelemetryService instance = DatabaseTelemetryService._();
  factory DatabaseTelemetryService() => instance;

  final _client = ApiClient.instance;

  static const Duration _pollInterval = Duration(seconds: 2);

  Timer? _timer;
  String? _deviceId;
  bool _disposed = false;
  bool _pollInFlight = false;
  bool _isRunning = false;

  bool get isRunning => _isRunning;
  DateTime? lastRecordedAt;
  String? lastError;

  double arduinoLat = 0.0;
  double arduinoLng = 0.0;
  double arduinoBearing = 0.0;
  double arduinoSpeed = 0.0;
  double arduinoHdop = 99.9;
  bool gpsFix = false;
  int gpsQuality = 0;
  int satelliteCount = 0;
  bool locationLoaded = false;

  String deviceMode = 'idle';
  int motorSpeed = 0;
  int waypointIndex = 0;
  bool autopilotActive = false;
  bool smartMoveActive = false;
  int obstacleLeft = 400;
  int obstacleRight = 400;
  double steerIntegral = 0.0;
  double lastHeading = 0.0;

  int waypointCount = 0;
  String routeEvent = 'none';
  String routeReason = '';
  int routeSeq = 0;
  int routeWpIndex = -1;
  int routeWpTotal = -1;
  double routeDistM = -1.0;
  bool headingValid = true;
  bool motorDisabled = false;
  int uptimeS = 0;
  double xte = 0.0;
  double arrivalRadius = 3.0;
  int wpElapsedS = 0;
  int wpTimeoutS = 120;
  double wpDistM = 0.0;

  bool wifiConnected = false;
  int wifiSignal = 0;
  int wifiRssi = 0;
  bool gsmConnected = false;
  int signalQuality = 0;
  double drHeading = 0.0;
  double drHeadingAcc = 999.0;
  bool drValid = false;
  int fusionMode = 0;

  final ValueNotifier<Map<String, dynamic>> telemetryNotifier = ValueNotifier(
    {},
  );

  void start({required String deviceId}) {
    _disposed = false;
    if (_deviceId == deviceId && _timer != null) {
      unawaited(fetchLatest());
      return;
    }

    _deviceId = deviceId;
    _timer?.cancel();
    unawaited(fetchLatest());
    _timer = Timer.periodic(_pollInterval, (_) => unawaited(fetchLatest()));
  }

  Future<void> fetchLatest() async {
    final deviceId = _deviceId;
    if (_disposed || _pollInFlight || deviceId == null) return;

    _pollInFlight = true;
    try {
      final query = Uri(
        queryParameters: {'device_id': deviceId, 'limit': '1'},
      ).query;
      final response = await _client.get('/telemetry?$query');
      final records = response['data'];
      if (records is! List || records.isEmpty) {
        _markOffline('Telemetry database kosong');
        return;
      }

      final record = records.first;
      if (record is! Map<String, dynamic>) {
        _markOffline('Format telemetry database tidak valid');
        return;
      }

      final raw = record['raw'];
      if (raw is! Map<String, dynamic>) {
        _markOffline('Field raw telemetry tidak valid');
        return;
      }

      final recordedAtRaw = record['recorded_at'];
      lastRecordedAt = recordedAtRaw is String
          ? DateTime.tryParse(recordedAtRaw)
          : null;
      _handleTelemetry(raw);
      lastError = null;
      _setRunning(true);
    } catch (e) {
      _markOffline(e.toString());
    } finally {
      _pollInFlight = false;
    }
  }

  Future<void> stop() async {
    _disposed = true;
    _timer?.cancel();
    _timer = null;
    _setRunning(false);
  }

  void _handleTelemetry(Map<String, dynamic> data) {
    final lat = _firstValue(data, const ['lat', 'latitude']);
    final lng = _firstValue(data, const ['lng', 'lon', 'longitude']);
    final gpsFixRaw = _firstValue(data, const [
      'gps_fix',
      'gps_locked',
      'gpsLock',
      'fix',
      'locked',
    ]);
    final satelliteRaw = _firstValue(data, const [
      'satellite_count',
      'satellites',
      'sat',
      'sats',
    ]);

    arduinoLat = _toDouble(lat, 0.0);
    arduinoLng = _toDouble(lng, 0.0);
    arduinoBearing = _toDouble(
      _firstValue(data, const ['bearing', 'course']),
      0.0,
    );
    arduinoSpeed = _toDouble(data['speed'], 0.0);
    arduinoHdop = _toDouble(data['hdop'], 99.9);
    gpsFix = _toBool(gpsFixRaw, false);
    gpsQuality = _toInt(data['gps_quality'], 0);
    satelliteCount = _toInt(satelliteRaw, 0);

    deviceMode = (data['mode'] ?? 'idle').toString();
    motorSpeed = _toInt(data['motor_speed'], 0);
    waypointIndex = _toInt(data['waypoint_index'], 0);
    autopilotActive = _toBool(data['autopilot_active'], false);
    smartMoveActive = _toBool(data['smart_move_active'], false);
    obstacleLeft = _toInt(data['obstacle_left'], 400);
    obstacleRight = _toInt(data['obstacle_right'], 400);
    steerIntegral = _toDouble(data['steer_integral'], 0.0);
    lastHeading = _toDouble(data['last_heading'], arduinoBearing);
    if (lastHeading == 0.0 && arduinoBearing != 0.0) {
      lastHeading = arduinoBearing;
    }

    waypointCount = _toInt(data['waypoint_count'], 0);
    routeEvent = (data['route_event'] ?? 'none').toString();
    routeReason = (data['route_reason'] ?? '').toString();
    routeSeq = _toInt(data['route_seq'], 0);
    routeWpIndex = _toInt(data['route_wp_index'], -1);
    routeWpTotal = _toInt(data['route_wp_total'], -1);
    routeDistM = _toDouble(data['route_dist_m'], -1.0);
    headingValid = _toBool(data['heading_valid'], true);
    motorDisabled = _toBool(data['motor_disabled'], false);
    uptimeS = _toInt(data['uptime_s'], 0);
    xte = _toDouble(data['xte'], 0.0);
    arrivalRadius = _toDouble(data['arrival_radius'], 3.0);
    wpElapsedS = _toInt(data['wp_elapsed_s'], 0);
    wpTimeoutS = _toInt(data['wp_timeout_s'], 120);
    wpDistM = _toDouble(data['wp_dist_m'], 0.0);

    wifiRssi = _toInt(_firstValue(data, const ['wifi_rssi', 'rssi']), 0);
    wifiSignal = _toInt(
      _firstValue(data, const [
        'wifi_signal',
        'signal_quality',
        'wifi_rssi',
        'rssi',
      ]),
      0,
    );
    wifiConnected = _toBool(
      _firstValue(data, const [
        'wifi_connected',
        'wifiConnected',
        'wifi',
        'wifi_status',
      ]),
      true,
    );
    // Backward-compatible aliases for older UI/service callers.
    gsmConnected = wifiConnected;
    signalQuality = wifiSignal;
    drHeading = _toDouble(data['dr_heading'], 0.0);
    drHeadingAcc = _toDouble(data['dr_heading_acc'], 999.0);
    drValid = _toBool(
      _firstValue(data, const ['dr_valid', 'dr_active']),
      false,
    );
    fusionMode = _toInt(data['fusion_mode'], 0);

    locationLoaded = gpsFix && (arduinoLat != 0.0 || arduinoLng != 0.0);

    final normalizedData = Map<String, dynamic>.from(data)
      ..['lat'] = arduinoLat
      ..['lng'] = arduinoLng
      ..['gps_fix'] = gpsFix
      ..['satellite_count'] = satelliteCount
      ..['gps_quality'] = gpsQuality
      ..['route_event'] = routeEvent
      ..['route_reason'] = routeReason
      ..['route_seq'] = routeSeq
      ..['hdop'] = arduinoHdop
      ..['location_loaded'] = locationLoaded
      ..['wifi_connected'] = wifiConnected
      ..['wifi_signal'] = wifiSignal
      ..['wifi_rssi'] = wifiRssi
      ..['signal_quality'] = wifiSignal
      ..['recorded_at'] = lastRecordedAt?.toIso8601String();
    telemetryNotifier.value = normalizedData;

    debugPrint(
      '[DB GPS] lat=${arduinoLat.toStringAsFixed(5)} '
      'lng=${arduinoLng.toStringAsFixed(5)} '
      'fix=$gpsFix locationLoaded=$locationLoaded sat=$satelliteCount '
      'hdop=${arduinoHdop.toStringAsFixed(2)} q=$gpsQuality',
    );
  }

  void _markOffline(String error) {
    lastError = error;
    _setRunning(false);
    debugPrint('[DB GPS] fetch failed: $error');
  }

  void _setRunning(bool running) {
    if (_isRunning == running) return;
    _isRunning = running;
    telemetryNotifier.value = Map<String, dynamic>.from(telemetryNotifier.value)
      ..['database_online'] = running;
  }

  int _toInt(dynamic v, int fallback) {
    if (v == null) return fallback;
    if (v is int) return v;
    if (v is num) return v.toInt();
    if (v is String) return int.tryParse(v.trim()) ?? fallback;
    return fallback;
  }

  double _toDouble(dynamic v, double fallback) {
    if (v == null) return fallback;
    if (v is num) return v.toDouble();
    if (v is String) return double.tryParse(v.trim()) ?? fallback;
    return fallback;
  }

  bool _toBool(dynamic v, bool fallback) {
    if (v == null) return fallback;
    if (v is bool) return v;
    if (v is num) return v != 0;
    if (v is String) {
      switch (v.trim().toLowerCase()) {
        case '1':
        case 'true':
        case 'yes':
        case 'y':
        case 'on':
        case 'fix':
        case 'fixed':
        case 'lock':
        case 'locked':
          return true;
        case '0':
        case 'false':
        case 'no':
        case 'n':
        case 'off':
        case 'none':
        case 'nofix':
        case 'no_fix':
        case 'unlock':
        case 'unlocked':
          return false;
      }
    }
    return fallback;
  }

  dynamic _firstValue(Map<String, dynamic> data, List<String> keys) {
    for (final key in keys) {
      if (data.containsKey(key) && data[key] != null) return data[key];
    }
    return null;
  }
}
