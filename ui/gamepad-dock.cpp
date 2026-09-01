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

	// 1. Barra Superior de Conexão
	QGroupBox *topBox = new QGroupBox(this);
	QHBoxLayout *topLayout = new QHBoxLayout(topBox);
	topLayout->setContentsMargins(6, 4, 6, 4);

	statusLabel = new QLabel(this);
	statusLabel->setText(obs_module_text("⚪ Nenhum controle detectado"));
	statusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #8b949e;");

	bluetoothBtn = new QPushButton(obs_module_text("🔗 Parear / Bluetooth (Windows)"), this);
	bluetoothBtn->setStyleSheet("QPushButton { padding: 4px 10px; font-weight: bold; }");
	connect(bluetoothBtn, &QPushButton::clicked, this, &GamepadDock::onOpenBluetoothClicked);

	refreshScenesBtn = new QPushButton(obs_module_text("🔄 Atualizar Cenas"), this);
	connect(refreshScenesBtn, &QPushButton::clicked, this, &GamepadDock::onRefreshScenesClicked);

	topLayout->addWidget(statusLabel);
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
	QGroupBox *grpRb = new QGroupBox(obs_module_text("Camada RB (Segure RB + Botão) -> Cenas 1 a 4 (Program)"), tabScenes);
	QGridLayout *gridRb = new QGridLayout(grpRb);
	combo_rb_a = new QComboBox(grpRb);
	combo_rb_b = new QComboBox(grpRb);
	combo_rb_x = new QComboBox(grpRb);
	combo_rb_y = new QComboBox(grpRb);

	gridRb->addWidget(new QLabel("RB + A (Cena 1):", grpRb), 0, 0);
	gridRb->addWidget(combo_rb_a, 0, 1);
	gridRb->addWidget(new QLabel("RB + B (Cena 2):", grpRb), 0, 2);
	gridRb->addWidget(combo_rb_b, 0, 3);
	gridRb->addWidget(new QLabel("RB + X (Cena 3):", grpRb), 1, 0);
	gridRb->addWidget(combo_rb_x, 1, 1);
	gridRb->addWidget(new QLabel("RB + Y (Cena 4):", grpRb), 1, 2);
	gridRb->addWidget(combo_rb_y, 1, 3);
	tabScenesLayout->addWidget(grpRb);

	// Grupo RT
	QGroupBox *grpRt = new QGroupBox(obs_module_text("Camada RT (Segure RT + Botão) -> Cenas 5 a 8 (Program)"), tabScenes);
	QGridLayout *gridRt = new QGridLayout(grpRt);
	combo_rt_a = new QComboBox(grpRt);
	combo_rt_b = new QComboBox(grpRt);
	combo_rt_x = new QComboBox(grpRt);
	combo_rt_y = new QComboBox(grpRt);

	gridRt->addWidget(new QLabel("RT + A (Cena 5):", grpRt), 0, 0);
	gridRt->addWidget(combo_rt_a, 0, 1);
	gridRt->addWidget(new QLabel("RT + B (Cena 6):", grpRt), 0, 2);
	gridRt->addWidget(combo_rt_b, 0, 3);
	gridRt->addWidget(new QLabel("RT + X (Cena 7):", grpRt), 1, 0);
	gridRt->addWidget(combo_rt_x, 1, 1);
	gridRt->addWidget(new QLabel("RT + Y (Cena 8):", grpRt), 1, 2);
	gridRt->addWidget(combo_rt_y, 1, 3);
	tabScenesLayout->addWidget(grpRt);

	// Grupo D-Pad & Ações da Mesa
	QGroupBox *grpDpad = new QGroupBox(obs_module_text("Mesa de Corte: Transição, Corte e D-Pad (Preview)"), tabScenes);
	QGridLayout *gridDpad = new QGridLayout(grpDpad);
	combo_dpad_up = new QComboBox(grpDpad);
	combo_dpad_down = new QComboBox(grpDpad);
	combo_dpad_left = new QComboBox(grpDpad);
	combo_dpad_right = new QComboBox(grpDpad);

	gridDpad->addWidget(new QLabel("D-Pad Cima (Preview):", grpDpad), 0, 0);
	gridDpad->addWidget(combo_dpad_up, 0, 1);
	gridDpad->addWidget(new QLabel("D-Pad Baixo (Preview):", grpDpad), 0, 2);
	gridDpad->addWidget(combo_dpad_down, 0, 3);
	gridDpad->addWidget(new QLabel("D-Pad Esquerda (Preview):", grpDpad), 1, 0);
	gridDpad->addWidget(combo_dpad_left, 1, 1);
	gridDpad->addWidget(new QLabel("D-Pad Direita (Preview):", grpDpad), 1, 2);
	gridDpad->addWidget(combo_dpad_right, 1, 3);

	QLabel *lblCutInfo = new QLabel(obs_module_text("⚡ <b>LB</b>: Corte Seco (Cut) direto para Program | 🎬 <b>LT</b>: Transição Suave (Preview -> Program)"), grpDpad);
	lblCutInfo->setStyleSheet("color: #58a6ff; font-size: 11px; padding: 4px;");
	gridDpad->addWidget(lblCutInfo, 2, 0, 1, 4);
	tabScenesLayout->addWidget(grpDpad);

	tabScenesLayout->addStretch();
	tabs->addTab(tabScenes, obs_module_text("Mesa de Corte & Cenas"));

	// Conectar alterações de cenas
	connect(combo_rb_a, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rb_b, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rb_x, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rb_y, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rt_a, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rt_b, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rt_x, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_rt_y, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_dpad_up, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_dpad_down, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_dpad_left, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);
	connect(combo_dpad_right, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onSceneMappingChanged);

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
		"<tr style='background-color: #21262d;'><th>Botão / Controle</th><th>Ação Padrão (Sem Modificador)</th><th>Segurando RB</th><th>Segurando RT</th></tr>"
		"<tr><td><b>Botão A</b></td><td>Chama <b>Preset PTZ 1</b></td><td>Chama <b>Cena 1</b> no Programa</td><td>Chama <b>Cena 5</b> no Programa</td></tr>"
		"<tr><td><b>Botão B</b></td><td>Chama <b>Preset PTZ 2</b></td><td>Chama <b>Cena 2</b> no Programa</td><td>Chama <b>Cena 6</b> no Programa</td></tr>"
		"<tr><td><b>Botão X</b></td><td>Chama <b>Preset PTZ 3</b></td><td>Chama <b>Cena 3</b> no Programa</td><td>Chama <b>Cena 7</b> no Programa</td></tr>"
		"<tr><td><b>Botão Y</b></td><td>Chama <b>Preset PTZ 4</b></td><td>Chama <b>Cena 4</b> no Programa</td><td>Chama <b>Cena 8</b> no Programa</td></tr>"
		"<tr><td><b>Bumper LB</b></td><td colspan='3'><b>Corte Seco (Cut)</b> imediato para o Programa</td></tr>"
		"<tr><td><b>Gatilho LT</b></td><td colspan='3'><b>Transição Suave</b> (Studio Mode: Preview -> Program)</td></tr>"
		"<tr><td><b>D-Pad (Direcionais)</b></td><td colspan='3'>Envia Cenas Pré-definidas para o <b>Preview</b></td></tr>"
		"<tr><td><b>Analógico Esquerdo</b></td><td colspan='3'>Move Pan e Tilt suavemente (com rampa física cinematográfica)</td></tr>"
		"<tr><td><b>R3 ou Start</b></td><td colspan='3'>Alterna entre <b>Controle Manual</b> e <b>Rastreamento Automático Facial</b></td></tr>"
		"</table>"
		"<p style='color: #8b949e; margin-top: 8px;'><i>Dica: O controle assume o modo manual automaticamente assim que você encostar no analógico. Ao terminar, aperte R3 para o Face Tracker voltar a enquadrar sozinho.</i></p>"
	));
	tabGuideLayout->addWidget(guideText);
	tabGuideLayout->addStretch();
	tabs->addTab(tabGuide, obs_module_text("Guia dos Controles"));

	rootLayout->addWidget(tabs);

	// Preenche lista de cenas inicial
	populateScenes();

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
	updateCombo(combo_rb_a, cfg.scene_rb_a);
	updateCombo(combo_rb_b, cfg.scene_rb_b);
	updateCombo(combo_rb_x, cfg.scene_rb_x);
	updateCombo(combo_rb_y, cfg.scene_rb_y);
	updateCombo(combo_rt_a, cfg.scene_rt_a);
	updateCombo(combo_rt_b, cfg.scene_rt_b);
	updateCombo(combo_rt_x, cfg.scene_rt_x);
	updateCombo(combo_rt_y, cfg.scene_rt_y);
	updateCombo(combo_dpad_up, cfg.preview_up);
	updateCombo(combo_dpad_down, cfg.preview_down);
	updateCombo(combo_dpad_left, cfg.preview_left);
	updateCombo(combo_dpad_right, cfg.preview_right);
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
	cfg.scene_rb_a = getScene(combo_rb_a);
	cfg.scene_rb_b = getScene(combo_rb_b);
	cfg.scene_rb_x = getScene(combo_rb_x);
	cfg.scene_rb_y = getScene(combo_rb_y);
	cfg.scene_rt_a = getScene(combo_rt_a);
	cfg.scene_rt_b = getScene(combo_rt_b);
	cfg.scene_rt_x = getScene(combo_rt_x);
	cfg.scene_rt_y = getScene(combo_rt_y);
	cfg.preview_up = getScene(combo_dpad_up);
	cfg.preview_down = getScene(combo_dpad_down);
	cfg.preview_left = getScene(combo_dpad_left);
	cfg.preview_right = getScene(combo_dpad_right);
}

void GamepadDock::onTimerUpdate()
{
	GamepadState state;
	bool ok = GamepadController::get_instance().poll_state(state);

	if (ok && state.connected) {
		statusLabel->setText(obs_module_text("🟢 Controle Conectado (Xbox / PS5)"));
		statusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #2ea44f;");
	} else {
		statusLabel->setText(obs_module_text("⚪ Nenhum controle detectado"));
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
	lbl_btn_lt->setStyleSheet(badge_style(state.trigger_left > 0.4f));
	lbl_btn_rt->setStyleSheet(badge_style(state.trigger_right > 0.4f));

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
	obs_data_set_default_string(props, "scene_rb_a", "");
	obs_data_set_default_string(props, "scene_rb_b", "");
	obs_data_set_default_string(props, "scene_rb_x", "");
	obs_data_set_default_string(props, "scene_rb_y", "");
	obs_data_set_default_string(props, "scene_rt_a", "");
	obs_data_set_default_string(props, "scene_rt_b", "");
	obs_data_set_default_string(props, "scene_rt_x", "");
	obs_data_set_default_string(props, "scene_rt_y", "");
	obs_data_set_default_string(props, "preview_up", "");
	obs_data_set_default_string(props, "preview_down", "");
	obs_data_set_default_string(props, "preview_left", "");
	obs_data_set_default_string(props, "preview_right", "");
}

void GamepadDock::save_properties(obs_data_t *props)
{
	GamepadSceneConfig &cfg = GamepadController::get_instance().get_scene_config();
	obs_data_set_string(props, "scene_rb_a", cfg.scene_rb_a.c_str());
	obs_data_set_string(props, "scene_rb_b", cfg.scene_rb_b.c_str());
	obs_data_set_string(props, "scene_rb_x", cfg.scene_rb_x.c_str());
	obs_data_set_string(props, "scene_rb_y", cfg.scene_rb_y.c_str());
	obs_data_set_string(props, "scene_rt_a", cfg.scene_rt_a.c_str());
	obs_data_set_string(props, "scene_rt_b", cfg.scene_rt_b.c_str());
	obs_data_set_string(props, "scene_rt_x", cfg.scene_rt_x.c_str());
	obs_data_set_string(props, "scene_rt_y", cfg.scene_rt_y.c_str());
	obs_data_set_string(props, "preview_up", cfg.preview_up.c_str());
	obs_data_set_string(props, "preview_down", cfg.preview_down.c_str());
	obs_data_set_string(props, "preview_left", cfg.preview_left.c_str());
	obs_data_set_string(props, "preview_right", cfg.preview_right.c_str());
}

void GamepadDock::load_properties(obs_data_t *props)
{
	GamepadSceneConfig &cfg = GamepadController::get_instance().get_scene_config();
	cfg.scene_rb_a = obs_data_get_string(props, "scene_rb_a");
	cfg.scene_rb_b = obs_data_get_string(props, "scene_rb_b");
	cfg.scene_rb_x = obs_data_get_string(props, "scene_rb_x");
	cfg.scene_rb_y = obs_data_get_string(props, "scene_rb_y");
	cfg.scene_rt_a = obs_data_get_string(props, "scene_rt_a");
	cfg.scene_rt_b = obs_data_get_string(props, "scene_rt_b");
	cfg.scene_rt_x = obs_data_get_string(props, "scene_rt_x");
	cfg.scene_rt_y = obs_data_get_string(props, "scene_rt_y");
	cfg.preview_up = obs_data_get_string(props, "preview_up");
	cfg.preview_down = obs_data_get_string(props, "preview_down");
	cfg.preview_left = obs_data_get_string(props, "preview_left");
	cfg.preview_right = obs_data_get_string(props, "preview_right");

	populateScenes();
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
