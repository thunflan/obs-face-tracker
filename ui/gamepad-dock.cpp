#include "gamepad-dock.hpp"
#include "../src/gamepad-controller.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QProgressBar>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <QMainWindow>
#include <QAction>
#include <QScrollArea>

static GamepadDock *s_gamepad_dock = nullptr;

static QString badge_style(bool active)
{
	if (active) {
		return "QLabel { background-color: #2ea44f; color: white; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 11px; }";
	} else {
		return "QLabel { background-color: #30363d; color: #8b949e; border-radius: 4px; padding: 4px 8px; font-size: 11px; }";
	}
}

GamepadDock::GamepadDock(QWidget *parent)
	: QFrame(parent)
{
	setWindowTitle(obs_module_text("Gamepad PTZ & Mesa de Corte"));
	setObjectName("GamepadPtzCutDock");

	QVBoxLayout *rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(8, 8, 8, 8);
	rootLayout->setSpacing(6);

	// 1. Barra Superior de Conexão e Seleção de Controles
	QGroupBox *topBox = new QGroupBox(this);
	QHBoxLayout *topLayout = new QHBoxLayout(topBox);
	topLayout->setContentsMargins(6, 4, 6, 4);
	topLayout->setSpacing(8);

	statusLabel = new QLabel(this);
	statusLabel->setText(obs_module_text("⚪ Nenhum controle ativo"));
	statusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #8b949e;");

	QLabel *lblDev = new QLabel(obs_module_text("🎮 Controle:"), this);
	lblDev->setStyleSheet("font-weight: bold; font-size: 12px;");

	deviceCombo = new QComboBox(this);
	deviceCombo->setMinimumWidth(230);
	connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onDeviceSelected);

	refreshDevicesBtn = new QPushButton(obs_module_text("🔄 Dispositivos"), this);
	refreshDevicesBtn->setStyleSheet("QPushButton { padding: 4px 8px; }");
	connect(refreshDevicesBtn, &QPushButton::clicked, this, &GamepadDock::onRefreshDevicesClicked);

	bluetoothBtn = new QPushButton(obs_module_text("🔗 Parear / Bluetooth (Windows)"), this);
	bluetoothBtn->setStyleSheet("QPushButton { padding: 4px 10px; font-weight: bold; }");
	connect(bluetoothBtn, &QPushButton::clicked, this, &GamepadDock::onOpenBluetoothClicked);

	refreshScenesBtn = new QPushButton(obs_module_text("🔄 Cenas"), this);
	connect(refreshScenesBtn, &QPushButton::clicked, this, &GamepadDock::onRefreshScenesClicked);

	topLayout->addWidget(statusLabel);
	topLayout->addWidget(lblDev);
	topLayout->addWidget(deviceCombo);
	topLayout->addWidget(refreshDevicesBtn);
	topLayout->addStretch();
	topLayout->addWidget(bluetoothBtn);
	topLayout->addWidget(refreshScenesBtn);
	rootLayout->addWidget(topBox);

	// 2. Abas de Conteúdo
	QTabWidget *tabs = new QTabWidget(this);

	// ==========================================
	// ABA 1: Mesa de Corte (Combinações de Cenas)
	// ==========================================
	QWidget *tabScenes = new QWidget(this);
	QVBoxLayout *tabScenesLayout = new QVBoxLayout(tabScenes);

	// Grupo RB
	// 1. Grupo D-Pad Direto (Cenas 1 a 4)
	QGroupBox *grpDpad = new QGroupBox(obs_module_text("D-Pad Direto (Sem Modificador) -> Cenas 1 a 4 (Program)"), tabScenes);
	QGridLayout *gridDpad = new QGridLayout(grpDpad);
	combo_dpad_up = new QComboBox(grpDpad);
	combo_dpad_down = new QComboBox(grpDpad);
	combo_dpad_left = new QComboBox(grpDpad);
	combo_dpad_right = new QComboBox(grpDpad);

	gridDpad->addWidget(new QLabel("D-Pad Cima (Cena 1):", grpDpad), 0, 0);
	gridDpad->addWidget(combo_dpad_up, 0, 1);
	gridDpad->addWidget(new QLabel("D-Pad Baixo (Cena 2):", grpDpad), 0, 2);
	gridDpad->addWidget(combo_dpad_down, 0, 3);
	gridDpad->addWidget(new QLabel("D-Pad Esquerda (Cena 3):", grpDpad), 1, 0);
	gridDpad->addWidget(combo_dpad_left, 1, 1);
	gridDpad->addWidget(new QLabel("D-Pad Direita (Cena 4):", grpDpad), 1, 2);
	gridDpad->addWidget(combo_dpad_right, 1, 3);
	tabScenesLayout->addWidget(grpDpad);

	// 2. Grupo RB + D-Pad (Cenas 5 a 8)
	QGroupBox *grpRbDpad = new QGroupBox(obs_module_text("Camada RB (Segure RB + D-Pad) -> Cenas 5 a 8 (Program)"), tabScenes);
	QGridLayout *gridRbDpad = new QGridLayout(grpRbDpad);
	combo_rb_dpad_up = new QComboBox(grpRbDpad);
	combo_rb_dpad_down = new QComboBox(grpRbDpad);
	combo_rb_dpad_left = new QComboBox(grpRbDpad);
	combo_rb_dpad_right = new QComboBox(grpRbDpad);

	gridRbDpad->addWidget(new QLabel("RB + D-Pad Cima (Cena 5):", grpRbDpad), 0, 0);
	gridRbDpad->addWidget(combo_rb_dpad_up, 0, 1);
	gridRbDpad->addWidget(new QLabel("RB + D-Pad Baixo (Cena 6):", grpRbDpad), 0, 2);
	gridRbDpad->addWidget(combo_rb_dpad_down, 0, 3);
	gridRbDpad->addWidget(new QLabel("RB + D-Pad Esquerda (Cena 7):", grpRbDpad), 1, 0);
	gridRbDpad->addWidget(combo_rb_dpad_left, 1, 1);
	gridRbDpad->addWidget(new QLabel("RB + D-Pad Direita (Cena 8):", grpRbDpad), 1, 2);
	gridRbDpad->addWidget(combo_rb_dpad_right, 1, 3);
	tabScenesLayout->addWidget(grpRbDpad);

	// 3. Grupo RT + D-Pad (Cenas 9 a 12)
	QGroupBox *grpRtDpad = new QGroupBox(obs_module_text("Camada RT (Segure RT + D-Pad) -> Cenas 9 a 12 (Program)"), tabScenes);
	QGridLayout *gridRtDpad = new QGridLayout(grpRtDpad);
	combo_rt_dpad_up = new QComboBox(grpRtDpad);
	combo_rt_dpad_down = new QComboBox(grpRtDpad);
	combo_rt_dpad_left = new QComboBox(grpRtDpad);
	combo_rt_dpad_right = new QComboBox(grpRtDpad);

	gridRtDpad->addWidget(new QLabel("RT + D-Pad Cima (Cena 9):", grpRtDpad), 0, 0);
	gridRtDpad->addWidget(combo_rt_dpad_up, 0, 1);
	gridRtDpad->addWidget(new QLabel("RT + D-Pad Baixo (Cena 10):", grpRtDpad), 0, 2);
	gridRtDpad->addWidget(combo_rt_dpad_down, 0, 3);
	gridRtDpad->addWidget(new QLabel("RT + D-Pad Esquerda (Cena 11):", grpRtDpad), 1, 0);
	gridRtDpad->addWidget(combo_rt_dpad_left, 1, 1);
	gridRtDpad->addWidget(new QLabel("RT + D-Pad Direita (Cena 12):", grpRtDpad), 1, 2);
	gridRtDpad->addWidget(combo_rt_dpad_right, 1, 3);
	tabScenesLayout->addWidget(grpRtDpad);

	// Banner explicativo das funções de Presets e Corte
	QLabel *lblPresetsInfo = new QLabel(obs_module_text(
		"🎯 <b>Presets PTZ</b>: Botões <b>A, B, X, Y</b> (Presets 1 a 4) | <b>RB + (A,B,X,Y)</b> (Presets 5 a 8) | <b>RT + (A,B,X,Y)</b> (Presets 9 a 12)<br>"
		"⚡ <b>Mesa de Corte</b>: <b>LB</b>: Corte Seco (Cut) | 🎬 <b>LT</b>: Transição Suave (Preview -> Program)"), tabScenes);
	lblPresetsInfo->setStyleSheet("background-color: #161b22; color: #58a6ff; font-size: 11px; padding: 6px; border: 1px solid #30363d; border-radius: 4px;");
	tabScenesLayout->addWidget(lblPresetsInfo);

	tabScenesLayout->addStretch();
	tabs->addTab(tabScenes, obs_module_text("Mesa de Corte & Cenas"));

	// Conectar alterações de cenas
	connect(combo_dpad_up, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_dpad_down, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_dpad_left, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_dpad_right, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);

	connect(combo_rb_dpad_up, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rb_dpad_down, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rb_dpad_left, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rb_dpad_right, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);

	connect(combo_rt_dpad_up, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rt_dpad_down, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rt_dpad_left, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rt_dpad_right, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);

	// ==========================================
	// ABA 2: Tela de Teste e Calibração ao Vivo
	// ==========================================
	QWidget *tabTest = new QWidget(this);
	QVBoxLayout *tabTestLayout = new QVBoxLayout(tabTest);

	QGroupBox *grpSticks = new QGroupBox(obs_module_text("Analógicos e Gatilhos (Telemetria em Tempo Real)"), tabTest);
	QGridLayout *gridSticks = new QGridLayout(grpSticks);

	stickLeftLabel = new QLabel("Analógico Esquerdo (Pan / Tilt): 0.0, 0.0", grpSticks);
	stickLeftXBar = new QProgressBar(grpSticks);
	stickLeftXBar->setRange(-100, 100);
	stickLeftXBar->setValue(0);
	stickLeftXBar->setFormat("Pan (Horizontal): %v%");

	stickLeftYBar = new QProgressBar(grpSticks);
	stickLeftYBar->setRange(-100, 100);
	stickLeftYBar->setValue(0);
	stickLeftYBar->setFormat("Tilt (Vertical): %v%");

	triggerLeftBar = new QProgressBar(grpSticks);
	triggerLeftBar->setRange(0, 100);
	triggerLeftBar->setValue(0);
	triggerLeftBar->setFormat("Gatilho LT (Transição / Zoom Out): %v%");

	triggerRightBar = new QProgressBar(grpSticks);
	triggerRightBar->setRange(0, 100);
	triggerRightBar->setValue(0);
	triggerRightBar->setFormat("Gatilho RT (Modificador / Zoom In): %v%");

	gridSticks->addWidget(stickLeftLabel, 0, 0, 1, 2);
	gridSticks->addWidget(stickLeftXBar, 1, 0);
	gridSticks->addWidget(stickLeftYBar, 1, 1);
	gridSticks->addWidget(triggerLeftBar, 2, 0);
	gridSticks->addWidget(triggerRightBar, 2, 1);
	tabTestLayout->addWidget(grpSticks);

	// Indicadores dos Botões
	QGroupBox *grpButtons = new QGroupBox(obs_module_text("Estado dos Botões Físicos"), tabTest);
	QHBoxLayout *btnRow1 = new QHBoxLayout();
	QHBoxLayout *btnRow2 = new QHBoxLayout();
	QVBoxLayout *grpBtnLayout = new QVBoxLayout(grpButtons);

	lbl_btn_a = new QLabel("A (Preset 1 / Cena 1)", grpButtons);
	lbl_btn_b = new QLabel("B (Preset 2 / Cena 2)", grpButtons);
	lbl_btn_x = new QLabel("X (Preset 3 / Cena 3)", grpButtons);
	lbl_btn_y = new QLabel("Y (Preset 4 / Cena 4)", grpButtons);

	lbl_btn_lb = new QLabel("LB (Corte Seco)", grpButtons);
	lbl_btn_rb = new QLabel("RB (Modificador 1)", grpButtons);
	lbl_btn_lt = new QLabel("LT (Transição)", grpButtons);
	lbl_btn_rt = new QLabel("RT (Modificador 2)", grpButtons);
	lbl_dpad = new QLabel("D-Pad (Preview)", grpButtons);
	lbl_mode = new QLabel("Modo: Rastreamento", grpButtons);

	lbl_btn_a->setStyleSheet(badge_style(false));
	lbl_btn_b->setStyleSheet(badge_style(false));
	lbl_btn_x->setStyleSheet(badge_style(false));
	lbl_btn_y->setStyleSheet(badge_style(false));
	lbl_btn_lb->setStyleSheet(badge_style(false));
	lbl_btn_rb->setStyleSheet(badge_style(false));
	lbl_btn_lt->setStyleSheet(badge_style(false));
	lbl_btn_rt->setStyleSheet(badge_style(false));
	lbl_dpad->setStyleSheet(badge_style(false));
	lbl_mode->setStyleSheet(badge_style(false));

	btnRow1->addWidget(lbl_btn_a);
	btnRow1->addWidget(lbl_btn_b);
	btnRow1->addWidget(lbl_btn_x);
	btnRow1->addWidget(lbl_btn_y);
	btnRow1->addWidget(lbl_btn_lb);
	btnRow2->addWidget(lbl_btn_rb);
	btnRow2->addWidget(lbl_btn_lt);
	btnRow2->addWidget(lbl_btn_rt);
	btnRow2->addWidget(lbl_dpad);
	btnRow2->addWidget(lbl_mode);

	grpBtnLayout->addLayout(btnRow1);
	grpBtnLayout->addLayout(btnRow2);
	tabTestLayout->addWidget(grpButtons);

	tabTestLayout->addStretch();
	tabs->addTab(tabTest, obs_module_text("Tela de Teste & Calibração"));

	// ==========================================
	// ABA 3: Guia e Infográfico dos Controles
	// ==========================================
	QWidget *tabGuide = new QWidget(this);
	QVBoxLayout *tabGuideLayout = new QVBoxLayout(tabGuide);
	QLabel *guideText = new QLabel(tabGuide);
	guideText->setTextFormat(Qt::RichText);
	guideText->setWordWrap(true);
	guideText->setText(obs_module_text(
		"<h3>🎮 Guia Rápido de Operação por Gamepad</h3>"
		"<table border='1' cellpadding='6' cellspacing='0' style='border-collapse: collapse; width: 100%; border-color: #30363d;'>"
		"<tr style='background-color: #21262d;'><th>Botão / Controle</th><th>Ação Direta (Sem Modificador)</th><th>Segurando Bumper RB</th><th>Segurando Gatilho RT</th></tr>"
		"<tr><td><b>Botão A</b></td><td>Chama <b>Preset PTZ 1</b></td><td>Chama <b>Preset PTZ 5</b></td><td>Chama <b>Preset PTZ 9</b></td></tr>"
		"<tr><td><b>Botão B</b></td><td>Chama <b>Preset PTZ 2</b></td><td>Chama <b>Preset PTZ 6</b></td><td>Chama <b>Preset PTZ 10</b></td></tr>"
		"<tr><td><b>Botão X</b></td><td>Chama <b>Preset PTZ 3</b></td><td>Chama <b>Preset PTZ 7</b></td><td>Chama <b>Preset PTZ 11</b></td></tr>"
		"<tr><td><b>Botão Y</b></td><td>Chama <b>Preset PTZ 4</b></td><td>Chama <b>Preset PTZ 8</b></td><td>Chama <b>Preset PTZ 12</b></td></tr>"
		"<tr><td><b>D-Pad Cima</b></td><td>Chama <b>Cena 1</b></td><td>Chama <b>Cena 5</b></td><td>Chama <b>Cena 9</b></td></tr>"
		"<tr><td><b>D-Pad Baixo</b></td><td>Chama <b>Cena 2</b></td><td>Chama <b>Cena 6</b></td><td>Chama <b>Cena 10</b></td></tr>"
		"<tr><td><b>D-Pad Esquerda</b></td><td>Chama <b>Cena 3</b></td><td>Chama <b>Cena 7</b></td><td>Chama <b>Cena 11</b></td></tr>"
		"<tr><td><b>D-Pad Direita</b></td><td>Chama <b>Cena 4</b></td><td>Chama <b>Cena 8</b></td><td>Chama <b>Cena 12</b></td></tr>"
		"<tr><td><b>Bumper LB</b></td><td colspan='3'><b>Corte Seco (Cut)</b> imediato para o Programa</td></tr>"
		"<tr><td><b>Gatilho LT</b></td><td colspan='3'><b>Transição Suave</b> (Studio Mode: Preview -> Program)</td></tr>"
		"<tr><td><b>Analógico Esquerdo</b></td><td colspan='3'>Move Pan e Tilt suavemente (com rampa física cinematográfica)</td></tr>"
		"<tr><td><b>R3 ou Start</b></td><td colspan='3'>Alterna entre <b>Controle Manual</b> e <b>Rastreamento Automático Facial</b></td></tr>"
		"</table>"
		"<p style='color: #8b949e; margin-top: 8px;'><i>Dica: O controle assume o modo manual automaticamente assim que você encostar no analógico. Ao terminar, aperte R3 para o Face Tracker voltar a enquadrar sozinho.</i></p>"
	));
	tabGuideLayout->addWidget(guideText);
	tabGuideLayout->addStretch();
	tabs->addTab(tabGuide, obs_module_text("Guia dos Controles"));

	rootLayout->addWidget(tabs);

	// Preenche lista de cenas e dispositivos inicial
	populateScenes();
	populateDevices();

	// Timer de polling e atualização da tela de teste (30 Hz)
	pollTimer = new QTimer(this);
	connect(pollTimer, &QTimer::timeout, this, &GamepadDock::onTimerUpdate);
	pollTimer->start(33);
}

GamepadDock::~GamepadDock()
{
	if (pollTimer) {
		pollTimer->stop();
	}
}

void GamepadDock::onOpenBluetoothClicked()
{
	QDesktopServices::openUrl(QUrl("ms-settings:bluetooth"));
}

void GamepadDock::populateDevices()
{
	deviceCombo->blockSignals(true);
	deviceCombo->clear();

	std::vector<ControllerDeviceInfo> devices = GamepadController::get_instance().get_available_devices();
	std::string curSelected = GamepadController::get_instance().get_selected_device();

	int selectIdx = 0;
	for (size_t i = 0; i < devices.size(); i++) {
		deviceCombo->addItem(QString::fromUtf8(devices[i].name.c_str()), QString::fromUtf8(devices[i].id.c_str()));
		if (!curSelected.empty() && devices[i].id == curSelected) {
			selectIdx = (int)i;
		}
	}

	deviceCombo->setCurrentIndex(selectIdx);
	deviceCombo->blockSignals(false);
}

void GamepadDock::onDeviceSelected(int index)
{
	if (index < 0) return;
	QString devId = deviceCombo->itemData(index).toString();
	GamepadController::get_instance().set_selected_device(devId.toUtf8().constData());
}

void GamepadDock::onRefreshDevicesClicked()
{
	populateDevices();
}

void GamepadDock::populateScenes()
{
	QStringList scenesList;
	scenesList << "(Nenhuma / Desativado)";

	if (obs_frontend_get_main_window()) {
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_source_t *src = scenes.sources.array[i];
			const char *name = obs_source_get_name(src);
			if (name)
				scenesList << QString::fromUtf8(name);
		}
		obs_frontend_source_list_free(&scenes);
	}

	auto updateCombo = [&](QComboBox *cb, const std::string &current) {
		cb->blockSignals(true);
		cb->clear();
		cb->addItems(scenesList);
		int idx = cb->findText(QString::fromUtf8(current.c_str()));
		if (idx >= 0)
			cb->setCurrentIndex(idx);
		else
			cb->setCurrentIndex(0);
		cb->blockSignals(false);
	};

	GamepadSceneConfig &cfg = GamepadController::get_instance().get_scene_config();
	updateCombo(combo_dpad_up, cfg.scene_dpad_up);
	updateCombo(combo_dpad_down, cfg.scene_dpad_down);
	updateCombo(combo_dpad_left, cfg.scene_dpad_left);
	updateCombo(combo_dpad_right, cfg.scene_dpad_right);

	updateCombo(combo_rb_dpad_up, cfg.scene_rb_dpad_up);
	updateCombo(combo_rb_dpad_down, cfg.scene_rb_dpad_down);
	updateCombo(combo_rb_dpad_left, cfg.scene_rb_dpad_left);
	updateCombo(combo_rb_dpad_right, cfg.scene_rb_dpad_right);

	updateCombo(combo_rt_dpad_up, cfg.scene_rt_dpad_up);
	updateCombo(combo_rt_dpad_down, cfg.scene_rt_dpad_down);
	updateCombo(combo_rt_dpad_left, cfg.scene_rt_dpad_left);
	updateCombo(combo_rt_dpad_right, cfg.scene_rt_dpad_right);
}

void GamepadDock::onRefreshScenesClicked()
{
	populateScenes();
}

void GamepadDock::onSceneMappingChanged()
{
	auto getScene = [](QComboBox *cb) -> std::string {
		if (cb->currentIndex() <= 0)
			return "";
		return cb->currentText().toUtf8().constData();
	};

	GamepadSceneConfig &cfg = GamepadController::get_instance().get_scene_config();
	cfg.scene_dpad_up = getScene(combo_dpad_up);
	cfg.scene_dpad_down = getScene(combo_dpad_down);
	cfg.scene_dpad_left = getScene(combo_dpad_left);
	cfg.scene_dpad_right = getScene(combo_dpad_right);

	cfg.scene_rb_dpad_up = getScene(combo_rb_dpad_up);
	cfg.scene_rb_dpad_down = getScene(combo_rb_dpad_down);
	cfg.scene_rb_dpad_left = getScene(combo_rb_dpad_left);
	cfg.scene_rb_dpad_right = getScene(combo_rb_dpad_right);

	cfg.scene_rt_dpad_up = getScene(combo_rt_dpad_up);
	cfg.scene_rt_dpad_down = getScene(combo_rt_dpad_down);
	cfg.scene_rt_dpad_left = getScene(combo_rt_dpad_left);
	cfg.scene_rt_dpad_right = getScene(combo_rt_dpad_right);
}

void GamepadDock::onTimerUpdate()
{
	GamepadState state;
	// Chama tick() continuamente para ler o controle e disparar trocas de cenas e presets!
	bool ok = GamepadController::get_instance().tick(0.033f, state);

	if (ok && state.connected) {
		std::string actName = GamepadController::get_instance().get_active_device_name();
		if (actName.empty())
			actName = "Controle Conectado";
		statusLabel->setText(QString("🟢 %1").arg(QString::fromUtf8(actName.c_str())));
		statusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #2ea44f;");
	} else {
		statusLabel->setText(obs_module_text("⚪ Nenhum controle ativo"));
		statusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #8b949e;");
	}

	// Atualiza mostrador dos analógicos
	int pan_pct = (int)std::round(state.pan_axis * 100.0f);
	int tilt_pct = (int)std::round(state.tilt_axis * 100.0f);
	stickLeftXBar->setValue(pan_pct);
	stickLeftYBar->setValue(tilt_pct);
	stickLeftLabel->setText(QString("Analógico Esquerdo: Pan = %1% | Tilt = %2%")
					.arg(pan_pct)
					.arg(tilt_pct));

	triggerLeftBar->setValue((int)std::round(state.trigger_left * 100.0f));
	triggerRightBar->setValue((int)std::round(state.trigger_right * 100.0f));

	// Atualiza Badges dos Botões
	lbl_btn_a->setStyleSheet(badge_style(state.btn_a));
	lbl_btn_b->setStyleSheet(badge_style(state.btn_b));
	lbl_btn_x->setStyleSheet(badge_style(state.btn_x));
	lbl_btn_y->setStyleSheet(badge_style(state.btn_y));
	lbl_btn_lb->setStyleSheet(badge_style(state.btn_lb));
	lbl_btn_rb->setStyleSheet(badge_style(state.btn_rb));
	lbl_btn_lt->setStyleSheet(badge_style(state.trigger_left > 0.4f || state.btn_lt));
	lbl_btn_rt->setStyleSheet(badge_style(state.trigger_right > 0.4f || state.btn_rt));

	bool dpad_any = (state.dpad_up || state.dpad_down || state.dpad_left || state.dpad_right);
	lbl_dpad->setStyleSheet(badge_style(dpad_any));

	if (state.manual_active) {
		lbl_mode->setText("Modo: Manual (Operador)");
		lbl_mode->setStyleSheet("QLabel { background-color: #d29922; color: black; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 11px; }");
	} else {
		lbl_mode->setText("Modo: Rastreamento Facial");
		lbl_mode->setStyleSheet("QLabel { background-color: #1f6feb; color: white; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 11px; }");
	}
}

void GamepadDock::default_properties(obs_data_t *props)
{
	obs_data_set_default_string(props, "selected_device", "auto");

	obs_data_set_default_string(props, "scene_dpad_up", "");
	obs_data_set_default_string(props, "scene_dpad_down", "");
	obs_data_set_default_string(props, "scene_dpad_left", "");
	obs_data_set_default_string(props, "scene_dpad_right", "");

	obs_data_set_default_string(props, "scene_rb_dpad_up", "");
	obs_data_set_default_string(props, "scene_rb_dpad_down", "");
	obs_data_set_default_string(props, "scene_rb_dpad_left", "");
	obs_data_set_default_string(props, "scene_rb_dpad_right", "");

	obs_data_set_default_string(props, "scene_rt_dpad_up", "");
	obs_data_set_default_string(props, "scene_rt_dpad_down", "");
	obs_data_set_default_string(props, "scene_rt_dpad_left", "");
	obs_data_set_default_string(props, "scene_rt_dpad_right", "");
}

void GamepadDock::save_properties(obs_data_t *props)
{
	obs_data_set_string(props, "selected_device", GamepadController::get_instance().get_selected_device().c_str());

	GamepadSceneConfig &cfg = GamepadController::get_instance().get_scene_config();
	obs_data_set_string(props, "scene_dpad_up", cfg.scene_dpad_up.c_str());
	obs_data_set_string(props, "scene_dpad_down", cfg.scene_dpad_down.c_str());
	obs_data_set_string(props, "scene_dpad_left", cfg.scene_dpad_left.c_str());
	obs_data_set_string(props, "scene_dpad_right", cfg.scene_dpad_right.c_str());

	obs_data_set_string(props, "scene_rb_dpad_up", cfg.scene_rb_dpad_up.c_str());
	obs_data_set_string(props, "scene_rb_dpad_down", cfg.scene_rb_dpad_down.c_str());
	obs_data_set_string(props, "scene_rb_dpad_left", cfg.scene_rb_dpad_left.c_str());
	obs_data_set_string(props, "scene_rb_dpad_right", cfg.scene_rb_dpad_right.c_str());

	obs_data_set_string(props, "scene_rt_dpad_up", cfg.scene_rt_dpad_up.c_str());
	obs_data_set_string(props, "scene_rt_dpad_down", cfg.scene_rt_dpad_down.c_str());
	obs_data_set_string(props, "scene_rt_dpad_left", cfg.scene_rt_dpad_left.c_str());
	obs_data_set_string(props, "scene_rt_dpad_right", cfg.scene_rt_dpad_right.c_str());
}

void GamepadDock::load_properties(obs_data_t *props)
{
	const char *selDev = obs_data_get_string(props, "selected_device");
	if (selDev && *selDev) {
		GamepadController::get_instance().set_selected_device(selDev);
	}

	GamepadSceneConfig &cfg = GamepadController::get_instance().get_scene_config();
	cfg.scene_dpad_up = obs_data_get_string(props, "scene_dpad_up");
	cfg.scene_dpad_down = obs_data_get_string(props, "scene_dpad_down");
	cfg.scene_dpad_left = obs_data_get_string(props, "scene_dpad_left");
	cfg.scene_dpad_right = obs_data_get_string(props, "scene_dpad_right");

	cfg.scene_rb_dpad_up = obs_data_get_string(props, "scene_rb_dpad_up");
	cfg.scene_rb_dpad_down = obs_data_get_string(props, "scene_rb_dpad_down");
	cfg.scene_rb_dpad_left = obs_data_get_string(props, "scene_rb_dpad_left");
	cfg.scene_rb_dpad_right = obs_data_get_string(props, "scene_rb_dpad_right");

	cfg.scene_rt_dpad_up = obs_data_get_string(props, "scene_rt_dpad_up");
	cfg.scene_rt_dpad_down = obs_data_get_string(props, "scene_rt_dpad_down");
	cfg.scene_rt_dpad_left = obs_data_get_string(props, "scene_rt_dpad_left");
	cfg.scene_rt_dpad_right = obs_data_get_string(props, "scene_rt_dpad_right");

	populateScenes();
	populateDevices();
}

static void save_load_gamepad_dock(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		obs_data_t *props = obs_data_create();
		if (s_gamepad_dock) {
			s_gamepad_dock->save_properties(props);
		}
		obs_data_set_obj(save_data, "gamepad_dock_config", props);
		obs_data_release(props);
	} else {
		obs_data_t *props = obs_data_get_obj(save_data, "gamepad_dock_config");
		if (props) {
			if (s_gamepad_dock) {
				s_gamepad_dock->load_properties(props);
			}
			obs_data_release(props);
		}
	}
}

void gamepad_dock_init()
{
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	s_gamepad_dock = new GamepadDock(main_window);

	// 1. Registra no menu Painéis (Docks) do OBS
	obs_frontend_add_dock_by_id("gamepad_ptz_cut_dock", obs_module_text("Gamepad PTZ & Mesa de Corte"), static_cast<QWidget *>(s_gamepad_dock));

	// 2. Registra no menu Ferramentas (Tools) do OBS
	QAction *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Gamepad PTZ & Mesa de Corte...")));
	if (action) {
		auto cb = [] {
			if (s_gamepad_dock) {
				s_gamepad_dock->show();
				s_gamepad_dock->raise();
			}
		};
		QAction::connect(action, &QAction::triggered, cb);
	}

	obs_frontend_add_save_callback(save_load_gamepad_dock, nullptr);
}

void gamepad_dock_release()
{
	s_gamepad_dock = nullptr;
}
