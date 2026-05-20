import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'controller_page.dart';
import 'core/api_client.dart';
import 'services/auth_service.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Lock landscape dari awal sebelum apapun tampil
  await SystemChrome.setPreferredOrientations([
    DeviceOrientation.landscapeLeft,
    DeviceOrientation.landscapeRight,
  ]);

  // ✅ SET BASE URL untuk API Client!
  ApiClient.instance.setBaseUrl(
    'https://spedi-core-production-0bb0.up.railway.app',
  );
  debugPrint('[MAIN] API Base URL di-set');

  // ✅ AUTO-LOGIN dengan credentials
  try {
    debugPrint('[MAIN] Mencoba auto-login...');
    final authService = AuthService();
    await authService.login('rama@spedi.io', 'pctspedi');
    debugPrint('[MAIN] ✅ Auto-login berhasil!');
  } catch (e) {
    debugPrint('[MAIN] ⚠️ Auto-login gagal: $e');
    debugPrint('[MAIN] Silakan cek credentials atau koneksi internet');
  }

  runApp(const ShipControllerApp());
}

class ShipControllerApp extends StatelessWidget {
  const ShipControllerApp({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SPEDI RC Controller',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        primaryColor: const Color(0xFF06B6D4),
        scaffoldBackgroundColor: const Color(0xFF020617),
      ),
      // ✅ LANGSUNG KE CONTROLLER - AUTO-LOGIN DI BACKGROUND!
      home: const ShipControllerPage(username: 'Guest'),
    );
  }
}
