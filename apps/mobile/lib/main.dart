import 'package:flutter/material.dart';

import 'screens/pair_screen.dart';
import 'state/engine.dart';
import 'theme.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const SmartMicApp());
}

class SmartMicApp extends StatefulWidget {
  const SmartMicApp({super.key});

  @override
  State<SmartMicApp> createState() => _SmartMicAppState();
}

class _SmartMicAppState extends State<SmartMicApp> {
  final _engine = SmartMicEngine();

  @override
  void dispose() {
    _engine.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SmartMic',
      debugShowCheckedModeBanner: false,
      theme: SmartMicTheme.build(),
      home: PairScreen(engine: _engine),
    );
  }
}
