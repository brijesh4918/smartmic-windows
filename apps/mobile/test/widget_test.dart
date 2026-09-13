// The engine and protocol are covered by the C++ suite (72 tests, see
// windows/audio-service/tests and tests/integration). This checks that the
// pairing screen builds and gates the Pair button until the form is complete --
// the one piece of logic that lives only in Dart.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:smartmic/screens/pair_screen.dart';
import 'package:smartmic/state/engine.dart';
import 'package:smartmic/theme.dart';

void main() {
  testWidgets('pair screen renders and disables Pair until the form is filled',
      (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: SmartMicTheme.build(),
      home: PairScreen(engine: SmartMicEngine()),
    ));

    expect(find.text('Pair with your PC'), findsOneWidget);
    expect(find.text('Paste pairing link'), findsOneWidget);

    final pairButton = tester.widget<FilledButton>(find.widgetWithText(FilledButton, 'Pair'));
    expect(pairButton.onPressed, isNull, reason: 'Pair must stay disabled until host, session and a six-digit code are present');
  });
}
