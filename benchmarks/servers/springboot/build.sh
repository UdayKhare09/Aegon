#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Building Spring Boot Benchmark Server..."
mvn -f "${SCRIPT_DIR}/pom.xml" clean package -DskipTests

cat << 'EOF' > "${SCRIPT_DIR}/bench_springboot"
#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JAR_PATH="${SCRIPT_DIR}/target/bench_springboot.jar"

if [ ! -f "${JAR_PATH}" ]; then
    echo "[bench_springboot] Target jar not found. Building..."
    mvn -f "${SCRIPT_DIR}/pom.xml" package -DskipTests
fi

exec java \
  -XX:+UseG1GC \
  -Xms512m -Xmx2g \
  -jar "${JAR_PATH}" "$@"
EOF

chmod +x "${SCRIPT_DIR}/bench_springboot"
echo "✓ Spring Boot Server built: ${SCRIPT_DIR}/bench_springboot"

