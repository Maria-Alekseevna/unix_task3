#!/bin/bash

WORK_DIR="/tmp/myinit_test"
CURRENT_DIR=$(pwd)

echo "=== MyInit Test Suite ==="
echo "Working directory: $WORK_DIR"

# Очистка предыдущих запусков
pkill -f "myinit" 2>/dev/null
pkill -f "test_prog" 2>/dev/null
rm -rf "$WORK_DIR"
sleep 1

mkdir -p "$WORK_DIR"

cp "$CURRENT_DIR/test_prog1" "$WORK_DIR/" 2>/dev/null
cp "$CURRENT_DIR/test_prog2" "$WORK_DIR/" 2>/dev/null
cp "$CURRENT_DIR/test_prog3" "$WORK_DIR/" 2>/dev/null

chmod +x "$WORK_DIR"/test_prog* 2>/dev/null

# Создаем тестовые конфигурационные файлы (с абсолютными путями)
cat > "$WORK_DIR/config1.txt" << EOF
$WORK_DIR/test_prog1 $WORK_DIR/in1 $WORK_DIR/out1
$WORK_DIR/test_prog2 $WORK_DIR/in2 $WORK_DIR/out2
$WORK_DIR/test_prog3 $WORK_DIR/in3 $WORK_DIR/out3
EOF

cat > "$WORK_DIR/config2.txt" << EOF
$WORK_DIR/test_prog1 $WORK_DIR/in1 $WORK_DIR/out1
EOF

touch "$WORK_DIR/in1" "$WORK_DIR/in2" "$WORK_DIR/in3"

echo ""
echo "Building programs..."
make clean
make

if [ ! -f myinit ]; then
    echo "ERROR: myinit not built"
    exit 1
fi

cp myinit "$WORK_DIR/"

echo ""
echo "Starting myinit..."
cd "$WORK_DIR"
# Запускаем в фоне
./myinit -c "$WORK_DIR/config1.txt" &
cd "$CURRENT_DIR"

sleep 3

# Функция для подсчета процессов
count_processes() {
    local pattern=$1
    pgrep -f "$pattern" 2>/dev/null | wc -l
}

echo ""
echo "=== Test 1: Check 3 processes running ==="
CHILD_COUNT=$(count_processes "test_prog")
echo "Found $CHILD_COUNT processes"
if [ "$CHILD_COUNT" -eq 3 ]; then
    echo "✓ PASSED: Found 3 child processes" | tee -a "$CURRENT_DIR/result.txt"
else
    echo "✗ FAILED: Found $CHILD_COUNT child processes (expected 3)" | tee -a "$CURRENT_DIR/result.txt"
fi

echo ""
echo "=== Test 2: Kill test_prog2 and verify restart ==="
pkill -f "test_prog2"
sleep 3

CHILD_COUNT=$(count_processes "test_prog")
echo "After kill, found $CHILD_COUNT processes"
if [ "$CHILD_COUNT" -eq 3 ]; then
    echo "✓ PASSED: After killing, still 3 child processes (restarted)" | tee -a "$CURRENT_DIR/result.txt"
else
    echo "✗ FAILED: After killing, found $CHILD_COUNT child processes (expected 3)" | tee -a "$CURRENT_DIR/result.txt"
fi

echo ""
echo "=== Test 3: Replace config and send SIGHUP ==="
cp "$WORK_DIR/config2.txt" "$WORK_DIR/config1.txt"

MYINIT_PID=$(pgrep -f "myinit" | head -1)
if [ -n "$MYINIT_PID" ]; then
    echo "Sending SIGHUP to myinit (PID: $MYINIT_PID)"
    kill -HUP $MYINIT_PID
    sleep 3
else
    echo "ERROR: myinit not running"
    exit 1
fi

CHILD_COUNT=$(count_processes "test_prog")
echo "After SIGHUP, found $CHILD_COUNT processes"
if [ "$CHILD_COUNT" -eq 1 ]; then
    echo "✓ PASSED: After SIGHUP, found 1 child process" | tee -a "$CURRENT_DIR/result.txt"
else
    echo "✗ FAILED: After SIGHUP, found $CHILD_COUNT child processes (expected 1)" | tee -a "$CURRENT_DIR/result.txt"
fi

echo ""
echo "=== MyInit Log File ==="
echo "----------------------------------------"
cat /tmp/myinit.log | tee -a "$CURRENT_DIR/result.txt"
echo "----------------------------------------"

echo ""
echo "=== Log Analysis ==="
if grep -q "Started process 0" /tmp/myinit.log && \
   grep -q "Started process 1" /tmp/myinit.log && \
   grep -q "Started process 2" /tmp/myinit.log; then
    echo "✓ PASSED: Found start events for all 3 processes" | tee -a "$CURRENT_DIR/result.txt"
else
    echo "✗ FAILED: Missing start events for processes" | tee -a "$CURRENT_DIR/result.txt"
fi

if grep -q "restarting" /tmp/myinit.log; then
    echo "✓ PASSED: Found restart event after process kill" | tee -a "$CURRENT_DIR/result.txt"
else
    echo "✗ FAILED: No restart event found" | tee -a "$CURRENT_DIR/result.txt"
fi

if grep -q "SIGHUP received" /tmp/myinit.log; then
    echo "✓ PASSED: Found SIGHUP reload event" | tee -a "$CURRENT_DIR/result.txt"
else
    echo "✗ FAILED: No SIGHUP event found" | tee -a "$CURRENT_DIR/result.txt"
fi

echo ""
echo "Cleaning up..."
kill -TERM $MYINIT_PID 2>/dev/null
sleep 1
pkill -f "test_prog" 2>/dev/null

echo ""
echo "=== Test Summary ==="
echo "All results saved to result.txt"
echo "Full log available at /tmp/myinit.log"