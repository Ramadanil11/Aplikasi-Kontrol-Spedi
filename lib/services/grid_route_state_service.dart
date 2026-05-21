import 'package:latlong2/latlong.dart';

class GridRouteStateService {
  GridRouteStateService._();
  static final GridRouteStateService instance = GridRouteStateService._();
  factory GridRouteStateService() => instance;

  List<LatLng> waypoints = [];
  bool isExecuting = false;
  String? activeRouteId;
  bool waitingRouteAck = false;
  int lastRouteSeq = 0;
  bool routeSeqInitialized = false;

  bool get hasRouteState =>
      waypoints.isNotEmpty || isExecuting || activeRouteId != null;

  void save({
    required List<LatLng> waypoints,
    required bool isExecuting,
    required String? activeRouteId,
    required bool waitingRouteAck,
    required int lastRouteSeq,
    required bool routeSeqInitialized,
  }) {
    this.waypoints = List<LatLng>.of(waypoints);
    this.isExecuting = isExecuting;
    this.activeRouteId = activeRouteId;
    this.waitingRouteAck = waitingRouteAck;
    this.lastRouteSeq = lastRouteSeq;
    this.routeSeqInitialized = routeSeqInitialized;
  }

  void clear() {
    waypoints = [];
    isExecuting = false;
    activeRouteId = null;
    waitingRouteAck = false;
    lastRouteSeq = 0;
    routeSeqInitialized = false;
  }
}
