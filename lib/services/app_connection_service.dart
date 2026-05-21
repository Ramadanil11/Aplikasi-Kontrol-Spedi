import 'dart:async';

import 'package:flutter/foundation.dart';

import '../core/api_client.dart';
import '../core/api_exception.dart';
import 'auth_service.dart';
import 'database_telemetry_service.dart';
import 'mqtt_device_service.dart';
import 'session_service.dart';
import 'websocket_service.dart';

class AppConnectionService {
  AppConnectionService._();
  static final AppConnectionService instance = AppConnectionService._();
  factory AppConnectionService() => instance;

  static const String deviceId = 'cfead5c1-4e4e-42da-af88-70620b8e3eac';
  static const String _fallbackEmail = 'rama@spedi.io';
  static const String _fallbackPassword = 'pctspedi';

  final _apiClient = ApiClient.instance;
  final _authService = AuthService();
  final _sessionService = SessionService.instance;
  final _wsService = WebSocketService.instance;
  final _mqttDevice = MqttDeviceService.instance;
  final _dbTelemetry = DatabaseTelemetryService.instance;

  Future<void>? _connectFuture;

  Future<void> ensureConnected({String source = 'app'}) {
    return _connectFuture ??= _ensureConnected(source).whenComplete(() {
      _connectFuture = null;
    });
  }

  Future<void> _ensureConnected(String source) async {
    debugPrint('[APP-CONN][$source] ensuring auth/session/control channels');

    if (!_apiClient.isAuthenticated) {
      debugPrint('[APP-CONN][$source] token kosong, login ulang');
      await _authService.login(_fallbackEmail, _fallbackPassword);
    }

    try {
      await _sessionService.openSession(deviceId);
    } on ApiException catch (e) {
      if (e.statusCode != 401 && e.statusCode != 403) rethrow;
      debugPrint('[APP-CONN][$source] token expired, login ulang');
      _apiClient.clearToken();
      await _authService.login(_fallbackEmail, _fallbackPassword);
      await _sessionService.openSession(deviceId);
    }

    if (_wsService.state != WsConnectionState.connected) {
      await _wsService.connect();
    }

    _dbTelemetry.start(deviceId: deviceId);

    if (!_mqttDevice.isRunning) {
      _mqttDevice.startAsync();
    }
  }
}
