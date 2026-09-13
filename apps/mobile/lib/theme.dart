import 'package:flutter/material.dart';

/// One place for the design decisions, so READY and LIVE can never drift into
/// looking similar.
class SmartMicTheme {
  static const bg = Color(0xFF0B0F14);
  static const surface = Color(0xFF141A22);
  static const surfaceHigh = Color(0xFF1D2732);
  static const border = Color(0xFF26323F);

  static const text = Color(0xFFE8EDF3);
  static const textDim = Color(0xFF8C9AAB);

  static const accent = Color(0xFF4DA3FF);   // ready / neutral
  static const live = Color(0xFF2BD98B);     // live
  static const warn = Color(0xFFFFB454);
  static const danger = Color(0xFFFF6B6B);

  static ThemeData build() {
    const scheme = ColorScheme.dark(
      primary: accent,
      secondary: live,
      surface: surface,
      error: danger,
    );
    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      scaffoldBackgroundColor: bg,
      fontFamily: 'Roboto',
      textTheme: const TextTheme(
        displaySmall: TextStyle(color: text, fontWeight: FontWeight.w600, letterSpacing: -0.5),
        titleLarge: TextStyle(color: text, fontWeight: FontWeight.w600),
        titleMedium: TextStyle(color: text, fontWeight: FontWeight.w500),
        bodyMedium: TextStyle(color: textDim, height: 1.4),
        labelLarge: TextStyle(color: text, fontWeight: FontWeight.w600, letterSpacing: 0.2),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: surfaceHigh,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: const BorderSide(color: border),
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: const BorderSide(color: border),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: const BorderSide(color: accent, width: 1.6),
        ),
        labelStyle: const TextStyle(color: textDim),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          minimumSize: const Size.fromHeight(54),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
          textStyle: const TextStyle(fontWeight: FontWeight.w600, fontSize: 16),
        ),
      ),
    );
  }
}
