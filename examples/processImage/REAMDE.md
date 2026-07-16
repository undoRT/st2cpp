# How to execute

To execute this example, copy st2cpp bin to this folder and execute:

~~~bash
./st2cpp --workspace . --project-style
~~~

Then:

~~~bash
cmake CMakeLists.txt

make
~~~

If it is ok, you should see "plc_test", execute it:

~~~bash
./plc_test
~~~

THe output should be:

~~~bash
============================================================
  PLC PROGRAM TEST - generated from Structured Text
============================================================

>>> Creating program instances...
    - MAIN program created
    - DIAGNOSTIC program created

>>> Creating function block instances...
    - MOTORCONTROLLER instance 1 created
    - MOTORCONTROLLER instance 2 created

>>> TEST 1: Input read (%IX) - Reading from process image
------------------------------------------------------------
    STARTBUTTON  = YES
    STOPBUTTON   = NO
    EMERGENCY    = NO
  ✓ PASS - Input read (%IX0.0, %IX0.1, %IX0.2)

>>> TEST 2: Output write (%QX, %QW) - Writing to process image
------------------------------------------------------------
    MOTOROUTPUT = YES
    ALARMLIGHT  = NO
    STATUSWORD  = 0x1234
  ✓ PASS - Output write (%QX0.0, %QX0.1, %QW2)

>>> TEST 3: Marker read/write (%MW, %MB, %ML) - Marker access
------------------------------------------------------------
    COUNTER     = 42
    TEMPERATURE = 75
    FLAGS       = 0xaa
    STATUS      = 0xdeadbeef
  ✓ PASS - Marker access (%MW10, %MW12, %MB20, %ML22)

>>> TEST 6: Counter increment
------------------------------------------------------------
    Cycle 1: COUNTER = 1
    Cycle 2: COUNTER = 2
    Cycle 3: COUNTER = 3
    Cycle 4: COUNTER = 4
    Cycle 5: COUNTER = 5
  ✓ PASS - Counter increment (IF INCREMENT THEN COUNTER := COUNTER + 1)

>>> TEST 7: Temperature update
------------------------------------------------------------
    NEWTEMP     = 85
    TEMPERATURE = 85
    STATUS.TEMP = 8.5
  ✓ PASS - Temperature update (Temperature := NewTemp; Status.TEMP := INT_TO_REAL(NewTemp)/10.0)

>>> TEST 8: Function block call
------------------------------------------------------------
    Motor1:
      ENABLE = YES
      SPEED  = 200
      RUNNING = NO
      FAULT   = NO
  ✓ PASS - Function block call (Motor(Enable := Start, Speed := 100))

>>> TEST 9: Function call
------------------------------------------------------------
    GETMOTORSPEED(1) = 0
    GETMOTORSPEED(2) = 0
  ✓ PASS - Function call (GETMOTORSPEED)

>>> TEST 10: Enum usage
------------------------------------------------------------
    IDLE     = 0
    RUNNING  = 1
    STOPPED  = 2
    ERROR    = 3
    Current STATE = 3
  ✓ PASS - Enum usage (TESTENUM)

>>> TEST 11: Struct usage
------------------------------------------------------------
    STATUS.Running = YES
    STATUS.Speed   = 123
    STATUS.Temp    = 45.6
    STATUS.Fault   = NO
  ✓ PASS - Struct usage (MotorStatus)

============================================================
  TEST SUMMARY
============================================================
    Tests passed: 9
    Tests failed: 0
    Total:        9
============================================================
  ✓ ALL TESTS PASSED - Process Image functionality verified!
============================================================
~~~
