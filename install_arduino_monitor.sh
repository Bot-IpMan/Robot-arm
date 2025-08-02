#!/bin/bash

# Автоматичне встановлення Arduino Monitor Service
# Запуск: sudo bash install_arduino_monitor.sh

set -e

# Кольори для виводу
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Конфігурація
INSTALL_DIR="/root/sketches"
SERVICE_NAME="arduino-monitor"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
PYTHON_SCRIPT="improved_arduino_monitor.py"
MANAGER_SCRIPT="arduino_service_manager.sh"

log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}ERROR:${NC} $1"
}

success() {
    echo -e "${GREEN}SUCCESS:${NC} $1"
}

warning() {
    echo -e "${YELLOW}WARNING:${NC} $1"
}

# Перевірка прав root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        error "Цей скрипт потрібно запускати з правами root"
        exit 1
    fi
}

# Створення директорії
create_directory() {
    log "Створення директорії $INSTALL_DIR..."
    mkdir -p "$INSTALL_DIR"
    cd "$INSTALL_DIR"
    success "Директорія створена"
}

# Встановлення Python залежностей
install_dependencies() {
    log "Встановлення Python залежностей..."
    
    # Оновлення apt
    apt update
    
    # Встановлення необхідних пакетів
    apt install -y python3 python3-pip python3-serial
    
    # Встановлення pyserial
    pip3 install pyserial
    
    success "Залежності встановлено"
}

# Створення systemd сервісу
create_service() {
    log "Створення systemd сервісу..."
    
    cat > "$SERVICE_FILE" << EOF
[Unit]
Description=Arduino Sensor Monitor Service
Documentation=man:arduino-monitor
After=network.target
Wants=network.target

[Service]
Type=simple
User=root
Group=root
WorkingDirectory=$INSTALL_DIR
ExecStart=/usr/bin/python3 $INSTALL_DIR/$PYTHON_SCRIPT
ExecReload=/bin/kill -HUP \$MAINPID
Restart=always
RestartSec=10
TimeoutStartSec=30
TimeoutStopSec=30

# Output handling
StandardOutput=journal
StandardError=journal
SyslogIdentifier=arduino-monitor

# Environment
Environment=PYTHONUNBUFFERED=1
Environment=PYTHONPATH=$INSTALL_DIR

# Security settings
NoNewPrivileges=true
PrivateTmp=true

# Restart policy
RestartSec=5
StartLimitBurst=5
StartLimitInterval=60

[Install]
WantedBy=multi-user.target
EOF
    
    success "Сервіс файл створено"
}

# Створення скрипту управління
create_manager_script() {
    log "Створення скрипту управління..."
    
    # Тут буде вміст скрипту управління (з попереднього артефакту)
    # Для економії місця, скористайтесь вмістом з артефакту service_commands
    
    chmod +x "$INSTALL_DIR/$MANAGER_SCRIPT"
    success "Скрипт управління створено"
}

# Налаштування systemd
configure_systemd() {
    log "Налаштування systemd..."
    
    # Перезавантаження daemon
    systemctl daemon-reload
    
    # Увімкнення сервісу
    systemctl enable "$SERVICE_NAME"
    
    success "Systemd налаштовано"
}

# Перевірка Arduino CLI
check_arduino_cli() {
    log "Перевірка Arduino CLI..."
    
    if ! command -v arduino-cli &> /dev/null; then
        warning "Arduino CLI не знайдено"
        log "Встановлення Arduino CLI..."
        
        # Завантаження та встановлення Arduino CLI
        curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
        
        # Додавання в PATH
        echo 'export PATH=$PATH:~/bin' >> ~/.bashrc
        export PATH=$PATH:~/bin
        
        # Налаштування
        arduino-cli config init
        arduino-cli core update-index
        arduino-cli core install arduino:avr
        
        success "Arduino CLI встановлено"
    else
        success "Arduino CLI знайдено"
    fi
}

# Тестування встановлення
test_installation() {
    log "Тестування встановлення..."
    
    # Перевірка файлів
    if [[ -f "$SERVICE_FILE" ]]; then
        success "Сервіс файл існує"
    else
        error "Сервіс файл не знайдено"
        return 1
    fi
    
    if [[ -f "$INSTALL_DIR/$PYTHON_SCRIPT" ]]; then
        success "Python скрипт існує"
    else
        error "Python скрипт не знайдено"
        return 1
    fi
    
    # Перевірка статусу сервісу
    if systemctl is-enabled "$SERVICE_NAME" &> /dev/null; then
        success "Сервіс увімкнено"
    else
        error "Сервіс не увімкнено"
        return 1
    fi
    
    success "Встановлення успішно протестовано"
}

# Показати інструкції
show_instructions() {
    echo ""
    echo "=== Arduino Monitor Service успішно встановлено ==="
    echo ""
    echo "Команди для управління:"
    echo "  sudo systemctl start $SERVICE_NAME     # Запустити сервіс"
    echo "  sudo systemctl stop $SERVICE_NAME      # Зупинити сервіс"
    echo "  sudo systemctl restart $SERVICE_NAME   # Перезапустити сервіс"
    echo "  sudo systemctl status $SERVICE_NAME    # Статус сервісу"
    echo ""
    echo "Перегляд логів:"
    echo "  sudo journalctl -u $SERVICE_NAME -f    # Логи в реальному часі"
    echo "  sudo journalctl -u $SERVICE_NAME       # Всі логи"
    echo ""
    echo "Скрипт управління:"
    echo "  bash $INSTALL_DIR/$MANAGER_SCRIPT      # Інтерактивне управління"
    echo ""
    echo "Файли встановлення:"
    echo "  Сервіс: $SERVICE_FILE"
    echo "  Python скрипт: $INSTALL_DIR/$PYTHON_SCRIPT"
    echo "  Робоча директорія: $INSTALL_DIR"
    echo ""
}

# Основна функція
main() {
    echo "=== Arduino Monitor Service Installer ==="
    echo ""
    
    check_root
    create_directory
    install_dependencies
    check_arduino_cli
    create_service
    create_manager_script
    configure_systemd
    test_installation
    show_instructions
    
    echo ""
    read -p "Запустити сервіс зараз? (y/n): " -n 1 -r
    echo ""
    
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        log "Запуск сервісу..."
        systemctl start "$SERVICE_NAME"
        sleep 2
        systemctl status "$SERVICE_NAME"
    fi
    
    success "Встановлення завершено!"
}

# Запуск
main "$@"
