import '../core/api_client.dart';

class UserModel {
  final String id;
  final String email;
  final bool isSuperuser;

  const UserModel({
    required this.id,
    required this.email,
    required this.isSuperuser,
  });

  factory UserModel.fromJson(Map<String, dynamic> json) => UserModel(
        id: json['id'] as String,
        email: json['email'] as String,
        isSuperuser: json['is_superuser'] as bool? ?? false,
      );
}

class AuthSession {
  final String accessToken;
  final String refreshToken;
  final int expiresIn;

  const AuthSession({
    required this.accessToken,
    required this.refreshToken,
    required this.expiresIn,
  });

  factory AuthSession.fromJson(Map<String, dynamic> json) => AuthSession(
        accessToken: json['access_token'] as String,
        refreshToken: json['refresh_token'] as String,
        expiresIn: json['expires_in'] as int,
      );
}

class LoginResult {
  final UserModel user;
  final AuthSession session;

  const LoginResult({required this.user, required this.session});
}

class AuthService {
  final _client = ApiClient.instance;

  /// Login dan otomatis simpan token ke ApiClient.
  /// 
  /// Throws [ApiException] jika credentials salah (401).
  Future<LoginResult> login(String email, String password) async {
    final data = await _client.post(
      '/auth/login',
      body: {'email': email, 'password': password},
      requireAuth: false,
    );

    final user = UserModel.fromJson(data['user'] as Map<String, dynamic>);
    final session = AuthSession.fromJson(data['session'] as Map<String, dynamic>);

    // Simpan token ke ApiClient agar request selanjutnya authenticated
    _client.setToken(session.accessToken);

    return LoginResult(user: user, session: session);
  }

  /// Register user baru.
  /// 
  /// Throws [ApiException] jika email sudah terdaftar (409).
  Future<LoginResult> register(String email, String password) async {
    final data = await _client.post(
      '/auth/register',
      body: {'email': email, 'password': password},
      requireAuth: false,
    );

    final user = UserModel.fromJson(data['user'] as Map<String, dynamic>);
    final session = AuthSession.fromJson(data['session'] as Map<String, dynamic>);

    _client.setToken(session.accessToken);

    return LoginResult(user: user, session: session);
  }
}
