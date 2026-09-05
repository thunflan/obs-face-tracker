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
#include <QSlider>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <QMainWindow>
#include <QAction>
#include <QScrollArea>
#include <QInputDialog>

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
	deviceCombo->setMinimumWidth(200);
	connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onDeviceSelected);

	refreshDevicesBtn = new QPushButton(obs_module_text("🔄 Dispositivos"), this);
	refreshDevicesBtn->setStyleSheet("QPushButton { padding: 4px 8px; }");
	connect(refreshDevicesBtn, &QPushButton::clicked, this, &GamepadDock::onRefreshDevicesClicked);

	cameraStatusLabel = new QLabel(this);
	cameraStatusLabel->setText(obs_module_text("🎥 PTZ: Auto (PTZ Controls)"));
	cameraStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #58a6ff; background: #161b22; padding: 4px 10px; border: 1px solid #30363d; border-radius: 4px;");

	bluetoothBtn = new QPushButton(obs_module_text("🔗 Parear / Bluetooth (Windows)"), this);
	bluetoothBtn->setStyleSheet("QPushButton { padding: 4px 10px; font-weight: bold; }");
	connect(bluetoothBtn, &QPushButton::clicked, this, &GamepadDock::onOpenBluetoothClicked);

	refreshScenesBtn = new QPushButton(obs_module_text("🔄 Cenas"), this);
	connect(refreshScenesBtn, &QPushButton::clicked, this, &GamepadDock::onRefreshScenesClicked);

	topLayout->addWidget(statusLabel);
	topLayout->addWidget(lblDev);
	topLayout->addWidget(deviceCombo);
	topLayout->addWidget(refreshDevicesBtn);
	topLayout->addWidget(cameraStatusLabel);
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
	// ABA 2: Mapeamento & Perfis (Estilo Emulador)
	// ==========================================
	QWidget *tabMapping = new QWidget(this);
	QVBoxLayout *tabMappingLayout = new QVBoxLayout(tabMapping);

	// Barra Superior do Perfil
	QGroupBox *grpProfile = new QGroupBox(obs_module_text("Perfis de Controle"), tabMapping);
	QHBoxLayout *profLayout = new QHBoxLayout(grpProfile);
	profLayout->setContentsMargins(6, 4, 6, 4);
	profLayout->setSpacing(8);

	QLabel *lblProf = new QLabel(obs_module_text("Perfil:"), grpProfile);
	profileCombo = new QComboBox(grpProfile);
	profileCombo->setMinimumWidth(180);
	connect(profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GamepadDock::onProfileSelected);

	btnNewProfile = new QPushButton(obs_module_text("➕ Novo"), grpProfile);
	connect(btnNewProfile, &QPushButton::clicked, this, &GamepadDock::onNewProfileClicked);

	btnSaveProfile = new QPushButton(obs_module_text("💾 Salvar"), grpProfile);
	connect(btnSaveProfile, &QPushButton::clicked, this, &GamepadDock::onSaveProfileClicked);

	btnResetXbox = new QPushButton(obs_module_text("🎮 Padrão Xbox"), grpProfile);
	connect(btnResetXbox, &QPushButton::clicked, this, &GamepadDock::onResetXboxProfileClicked);

	btnResetPS = new QPushButton(obs_module_text("🎮 Padrão PS"), grpProfile);
	connect(btnResetPS, &QPushButton::clicked, this, &GamepadDock::onResetPlaystationProfileClicked);

	btnDeleteProfile = new QPushButton(obs_module_text("🗑️ Excluir"), grpProfile);
	connect(btnDeleteProfile, &QPushButton::clicked, this, &GamepadDock::onDeleteProfileClicked);

	profLayout->addWidget(lblProf);
	profLayout->addWidget(profileCombo);
	profLayout->addWidget(btnNewProfile);
	profLayout->addWidget(btnSaveProfile);
	profLayout->addWidget(btnResetXbox);
	profLayout->addWidget(btnResetPS);
	profLayout->addWidget(btnDeleteProfile);
	profLayout->addStretch();
	tabMappingLayout->addWidget(grpProfile);

	// Assistente e Status
	QHBoxLayout *wizLayout = new QHBoxLayout();
	btnWizard = new QPushButton(obs_module_text("🧙 Assistente de Mapeamento Completo (Passo a Passo)"), tabMapping);
	btnWizard->setStyleSheet("QPushButton { font-weight: bold; padding: 6px 12px; background-color: #238636; color: white; border-radius: 4px; } QPushButton:hover { background-color: #2ea44f; }");
	connect(btnWizard, &QPushButton::clicked, this, &GamepadDock::onStartWizardClicked);

	btnCancelListen = new QPushButton(obs_module_text("⏹️ Cancelar Escuta"), tabMapping);
	btnCancelListen->setStyleSheet("QPushButton { font-weight: bold; padding: 6px 12px; background-color: #da3633; color: white; border-radius: 4px; } QPushButton:hover { background-color: #f85149; }");
	connect(btnCancelListen, &QPushButton::clicked, this, &GamepadDock::onCancelListenClicked);

	btnSkipStep = new QPushButton(obs_module_text("⏭️ Pular Botão"), tabMapping);
	btnSkipStep->setStyleSheet("QPushButton { font-weight: bold; padding: 6px 12px; background-color: #30363d; color: white; border-radius: 4px; } QPushButton:hover { background-color: #484f58; }");
	connect(btnSkipStep, &QPushButton::clicked, this, &GamepadDock::onSkipStepClicked);

	lblRebindStatus = new QLabel(obs_module_text("Status: Pronto. Escolha uma ação abaixo ou inicie o assistente."), tabMapping);
	lblRebindStatus->setStyleSheet("font-size: 11px; color: #58a6ff; font-weight: bold;");

	wizLayout->addWidget(btnWizard);
	wizLayout->addWidget(btnCancelListen);
	wizLayout->addWidget(btnSkipStep);
	wizLayout->addWidget(lblRebindStatus);
	wizLayout->addStretch();
	tabMappingLayout->addLayout(wizLayout);

	// Tabela / Grade de Mapeamento com Scroll
	QScrollArea *scroll = new QScrollArea(tabMapping);
	scroll->setWidgetResizable(true);
	scroll->setStyleSheet("QScrollArea { border: 1px solid #30363d; background: #0d1117; }");
	QWidget *scrollContent = new QWidget(scroll);
	QGridLayout *gridRemap = new QGridLayout(scrollContent);
	gridRemap->setContentsMargins(8, 8, 8, 8);
	gridRemap->setHorizontalSpacing(12);
	gridRemap->setVerticalSpacing(6);

	// Cabeçalho
	QLabel *hAction = new QLabel("Ação Virtual (Função)", scrollContent);
	hAction->setStyleSheet("font-weight: bold; color: #8b949e;");
	QLabel *hBind = new QLabel("Entrada Vinculada Atual", scrollContent);
	hBind->setStyleSheet("font-weight: bold; color: #8b949e;");
	QLabel *hMap = new QLabel("Mapear", scrollContent);
	hMap->setStyleSheet("font-weight: bold; color: #8b949e;");
	QLabel *hClr = new QLabel("Limpar", scrollContent);
	hClr->setStyleSheet("font-weight: bold; color: #8b949e;");

	gridRemap->addWidget(hAction, 0, 0);
	gridRemap->addWidget(hBind, 0, 1);
	gridRemap->addWidget(hMap, 0, 2);
	gridRemap->addWidget(hClr, 0, 3);

	rebindRows.resize((int)VirtualAction::Count);

	const char *actionDescriptions[(int)VirtualAction::Count] = {
		"Botão A (Preset 1 / Seleção)",
		"Botão B (Preset 2 / Voltar)",
		"Botão X (Preset 3)",
		"Botão Y (Preset 4)",
		"Bumper LB (Corte Seco)",
		"Bumper RB (Modificador 1)",
		"Gatilho LT (Transição)",
		"Gatilho RT (Modificador 2)",
		"D-Pad Cima (Cena 1)",
		"D-Pad Baixo (Cena 2)",
		"D-Pad Esquerda (Cena 3)",
		"D-Pad Direita (Cena 4)",
		"Menu / Start (Modo Manual/Auto)",
		"View / Back / Select",
		"Analógico L3 (Clique Esquerdo)",
		"Analógico R3 (Clique Direito)"
	};

	for (int i = 0; i < (int)VirtualAction::Count; i++) {
		QLabel *lAct = new QLabel(actionDescriptions[i], scrollContent);
		lAct->setStyleSheet("font-weight: 500; font-size: 11px;");

		QLabel *lCur = new QLabel("Padrão", scrollContent);
		lCur->setStyleSheet("color: #58a6ff; font-family: monospace; font-size: 11px; background: #161b22; padding: 2px 6px; border-radius: 3px; border: 1px solid #30363d;");

		QPushButton *bMap = new QPushButton("🎯 Mapear", scrollContent);
		bMap->setStyleSheet("padding: 3px 10px; font-size: 11px;");
		connect(bMap, &QPushButton::clicked, [this, i]() {
			this->onRebindButtonClicked(i);
		});

		QPushButton *bClr = new QPushButton("✖", scrollContent);
		bClr->setStyleSheet("padding: 3px 6px; font-size: 11px; color: #f85149;");
		connect(bClr, &QPushButton::clicked, [this, i]() {
			this->onClearBindingClicked(i);
		});

		rebindRows[i] = {lAct, lCur, bMap, bClr};

		gridRemap->addWidget(lAct, i + 1, 0);
		gridRemap->addWidget(lCur, i + 1, 1);
		gridRemap->addWidget(bMap, i + 1, 2);
		gridRemap->addWidget(bClr, i + 1, 3);
	}

	scroll->setWidget(scrollContent);
	tabMappingLayout->addWidget(scroll);
	tabs->addTab(tabMapping, obs_module_text("🎮 Mapeamento & Perfis"));

	// ==========================================
	// ABA 3: Tela de Teste e Calibração ao Vivo
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

	stickRightYBar = new QProgressBar(grpSticks);
	stickRightYBar->setRange(-100, 100);
	stickRightYBar->setValue(0);
	stickRightYBar->setFormat("Stick Dir / Zoom: %v%");

	triggerLeftBar = new QProgressBar(grpSticks);
	triggerLeftBar->setRange(0, 100);
	triggerLeftBar->setValue(0);
	triggerLeftBar->setFormat("Gatilho LT (Transição): %v%");

	triggerRightBar = new QProgressBar(grpSticks);
	triggerRightBar->setRange(0, 100);
	triggerRightBar->setValue(0);
	triggerRightBar->setFormat("Gatilho RT (Modificador): %v%");

	gridSticks->addWidget(stickLeftLabel, 0, 0, 1, 2);
	gridSticks->addWidget(stickLeftXBar, 1, 0);
	gridSticks->addWidget(stickLeftYBar, 1, 1);
	gridSticks->addWidget(triggerLeftBar, 2, 0);
	gridSticks->addWidget(triggerRightBar, 2, 1);
	gridSticks->addWidget(stickRightYBar, 3, 0, 1, 2);
	tabTestLayout->addWidget(grpSticks);

	// Indicadores dos 16 Botões Físicos
	QGroupBox *grpButtons = new QGroupBox(obs_module_text("Estado dos 16 Botões Físicos"), tabTest);
	QVBoxLayout *grpBtnLayout = new QVBoxLayout(grpButtons);
	QHBoxLayout *btnRow1 = new QHBoxLayout();
	QHBoxLayout *btnRow2 = new QHBoxLayout();
	QHBoxLayout *btnRow3 = new QHBoxLayout();
	QHBoxLayout *btnRow4 = new QHBoxLayout();

	lbl_btn_a = new QLabel("A (Preset 1)", grpButtons);
	lbl_btn_b = new QLabel("B (Preset 2)", grpButtons);
	lbl_btn_x = new QLabel("X (Preset 3)", grpButtons);
	lbl_btn_y = new QLabel("Y (Preset 4)", grpButtons);

	lbl_btn_lb = new QLabel("LB (Corte Seco)", grpButtons);
	lbl_btn_rb = new QLabel("RB (Mod 1)", grpButtons);
	lbl_btn_lt = new QLabel("LT (Trans)", grpButtons);
	lbl_btn_rt = new QLabel("RT (Mod 2)", grpButtons);

	lbl_dpad_up = new QLabel("D-Pad Cima", grpButtons);
	lbl_dpad_down = new QLabel("D-Pad Baixo", grpButtons);
	lbl_dpad_left = new QLabel("D-Pad Esq", grpButtons);
	lbl_dpad_right = new QLabel("D-Pad Dir", grpButtons);

	lbl_btn_start = new QLabel("Start / Menu", grpButtons);
	lbl_btn_back = new QLabel("Back / View", grpButtons);
	lbl_btn_thumb_l = new QLabel("L3 (Stick E)", grpButtons);
	lbl_btn_thumb_r = new QLabel("R3 (Stick D)", grpButtons);
	lbl_mode = new QLabel("Modo: Rastreamento", grpButtons);

	lbl_btn_a->setStyleSheet(badge_style(false));
	lbl_btn_b->setStyleSheet(badge_style(false));
	lbl_btn_x->setStyleSheet(badge_style(false));
	lbl_btn_y->setStyleSheet(badge_style(false));
	lbl_btn_lb->setStyleSheet(badge_style(false));
	lbl_btn_rb->setStyleSheet(badge_style(false));
	lbl_btn_lt->setStyleSheet(badge_style(false));
	lbl_btn_rt->setStyleSheet(badge_style(false));
	lbl_dpad_up->setStyleSheet(badge_style(false));
	lbl_dpad_down->setStyleSheet(badge_style(false));
	lbl_dpad_left->setStyleSheet(badge_style(false));
	lbl_dpad_right->setStyleSheet(badge_style(false));
	lbl_btn_start->setStyleSheet(badge_style(false));
	lbl_btn_back->setStyleSheet(badge_style(false));
	lbl_btn_thumb_l->setStyleSheet(badge_style(false));
	lbl_btn_thumb_r->setStyleSheet(badge_style(false));
	lbl_mode->setStyleSheet(badge_style(false));

	btnRow1->addWidget(lbl_btn_a);
	btnRow1->addWidget(lbl_btn_b);
	btnRow1->addWidget(lbl_btn_x);
	btnRow1->addWidget(lbl_btn_y);

	btnRow2->addWidget(lbl_btn_lb);
	btnRow2->addWidget(lbl_btn_rb);
	btnRow2->addWidget(lbl_btn_lt);
	btnRow2->addWidget(lbl_btn_rt);

	btnRow3->addWidget(lbl_dpad_up);
	btnRow3->addWidget(lbl_dpad_down);
	btnRow3->addWidget(lbl_dpad_left);
	btnRow3->addWidget(lbl_dpad_right);

	btnRow4->addWidget(lbl_btn_start);
	btnRow4->addWidget(lbl_btn_back);
	btnRow4->addWidget(lbl_btn_thumb_l);
	btnRow4->addWidget(lbl_btn_thumb_r);
	btnRow4->addWidget(lbl_mode);

	grpBtnLayout->addLayout(btnRow1);
	grpBtnLayout->addLayout(btnRow2);
	grpBtnLayout->addLayout(btnRow3);
	grpBtnLayout->addLayout(btnRow4);
	tabTestLayout->addWidget(grpButtons);

	// Monitor de Entrada Bruta (Sniffer)
	QGroupBox *grpSniffer = new QGroupBox(obs_module_text("📡 Monitor de Entrada de Hardware Bruta (Sniffer de Botões & Eixos)"), tabTest);
	QVBoxLayout *snifferLayout = new QVBoxLayout(grpSniffer);
	lblRawInput = new QLabel("Última entrada física detectada: Nenhuma", grpSniffer);
	lblRawInput->setStyleSheet("background: #0d1117; color: #58a6ff; font-family: Consolas, monospace; font-size: 12px; padding: 6px; border: 1px solid #30363d; border-radius: 4px;");
	snifferLayout->addWidget(lblRawInput);
	tabTestLayout->addWidget(grpSniffer);

	// Grupo de Calibração de Curvas e Sensibilidade PTZ
	QGroupBox *grpCurves = new QGroupBox(obs_module_text("⚙️ Ajustes de Velocidade Progressiva e Sensibilidade PTZ"), tabTest);
	QGridLayout *gridCurves = new QGridLayout(grpCurves);

	// 1. Curva Exponencial
	sliderCurve = new QSlider(Qt::Horizontal, grpCurves);
	sliderCurve->setRange(10, 30);
	sliderCurve->setValue(22);
	lblCurveVal = new QLabel("2.2x (Progressivo Suave)", grpCurves);
	lblCurveVal->setStyleSheet("font-weight: bold; color: #58a6ff;");
	connect(sliderCurve, &QSlider::valueChanged, this, &GamepadDock::onCurveGammaChanged);

	// 2. Velocidade Mínima (Creep)
	sliderMinSpeed = new QSlider(Qt::Horizontal, grpCurves);
	sliderMinSpeed->setRange(1, 20);
	sliderMinSpeed->setValue(4);
	lblMinSpeedVal = new QLabel("4%", grpCurves);
	lblMinSpeedVal->setStyleSheet("font-weight: bold; color: #58a6ff;");
	connect(sliderMinSpeed, &QSlider::valueChanged, this, &GamepadDock::onMinSpeedChanged);

	// 3. Velocidade Máxima Pan/Tilt
	sliderMaxSpeed = new QSlider(Qt::Horizontal, grpCurves);
	sliderMaxSpeed->setRange(20, 100);
	sliderMaxSpeed->setValue(100);
	lblMaxSpeedVal = new QLabel("100%", grpCurves);
	lblMaxSpeedVal->setStyleSheet("font-weight: bold; color: #58a6ff;");
	connect(sliderMaxSpeed, &QSlider::valueChanged, this, &GamepadDock::onMaxSpeedChanged);

	// 4. Velocidade Zoom
	sliderZoomSpeed = new QSlider(Qt::Horizontal, grpCurves);
	sliderZoomSpeed->setRange(20, 100);
	sliderZoomSpeed->setValue(80);
	lblZoomSpeedVal = new QLabel("80%", grpCurves);
	lblZoomSpeedVal->setStyleSheet("font-weight: bold; color: #58a6ff;");
	connect(sliderZoomSpeed, &QSlider::valueChanged, this, &GamepadDock::onZoomSpeedChanged);

	// 5. Zona Morta (Deadzone)
	sliderDeadzone = new QSlider(Qt::Horizontal, grpCurves);
	sliderDeadzone->setRange(0, 25);
	sliderDeadzone->setValue(12);
	lblDeadzoneVal = new QLabel("12%", grpCurves);
	lblDeadzoneVal->setStyleSheet("font-weight: bold; color: #58a6ff;");
	connect(sliderDeadzone, &QSlider::valueChanged, this, &GamepadDock::onDeadzoneChanged);

	gridCurves->addWidget(new QLabel(obs_module_text("Curva de Aceleração (Suavidade Progressiva):"), grpCurves), 0, 0);
	gridCurves->addWidget(sliderCurve, 0, 1);
	gridCurves->addWidget(lblCurveVal, 0, 2);

	gridCurves->addWidget(new QLabel(obs_module_text("Velocidade Mínima (Ajuste Fino Inicial):"), grpCurves), 1, 0);
	gridCurves->addWidget(sliderMinSpeed, 1, 1);
	gridCurves->addWidget(lblMinSpeedVal, 1, 2);

	gridCurves->addWidget(new QLabel(obs_module_text("Velocidade Máxima (Pan / Tilt):"), grpCurves), 2, 0);
	gridCurves->addWidget(sliderMaxSpeed, 2, 1);
	gridCurves->addWidget(lblMaxSpeedVal, 2, 2);

	gridCurves->addWidget(new QLabel(obs_module_text("Velocidade de Zoom (Stick Direito):"), grpCurves), 3, 0);
	gridCurves->addWidget(sliderZoomSpeed, 3, 1);
	gridCurves->addWidget(lblZoomSpeedVal, 3, 2);

	gridCurves->addWidget(new QLabel(obs_module_text("Zona Morta (Deadzone do Analógico):"), grpCurves), 4, 0);
	gridCurves->addWidget(sliderDeadzone, 4, 1);
	gridCurves->addWidget(lblDeadzoneVal, 4, 2);

	tabTestLayout->addWidget(grpCurves);

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
		"<tr><td><b>Analógico Esquerdo</b></td><td colspan='3'>Move <b>Pan e Tilt</b> com velocidade progressiva (lento ao mover pouco, rápido no fim)</td></tr>"
		"<tr><td><b>Analógico Direito</b></td><td colspan='3'><b>Zoom In</b> (empurrar para cima) / <b>Zoom Out</b> (puxar para baixo) com velocidade progressiva</td></tr>"
		"<tr><td><b>R3 ou Start</b></td><td colspan='3'>Alterna entre <b>Controle Manual</b> e <b>Rastreamento Automático Facial</b></td></tr>"
		"</table>"
		"<p style='color: #8b949e; margin-top: 8px;'><i>Dica: A câmera controlada é sempre sincronizada automaticamente com a seleção do PTZ Controls. O controle assume o modo manual assim que você mexer nos analógicos.</i></p>"
	));
	tabGuideLayout->addWidget(guideText);
	tabGuideLayout->addStretch();
	tabs->addTab(tabGuide, obs_module_text("Guia dos Controles"));

	rootLayout->addWidget(tabs);

	wizardStepIndex = 0;
	isWizardActive = false;

	// Preenche lista de cenas, dispositivos e perfis inicial
	populateScenes();
	populateDevices();
	populateProfiles();
	updateRebindUI();

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

void GamepadDock::onCurveGammaChanged(int val)
{
	float gamma = (float)val / 10.0f;
	GamepadController::get_instance().set_curve_gamma(gamma);
	if (lblCurveVal) {
		QString desc = (gamma >= 2.0f) ? "(Progressivo Suave)" : ((gamma <= 1.2f) ? "(Linear / Rápido)" : "(Moderado)");
		lblCurveVal->setText(QString("%1x %2").arg(gamma, 0, 'f', 1).arg(desc));
	}
}

void GamepadDock::onMinSpeedChanged(int val)
{
	float s = (float)val / 100.0f;
	GamepadController::get_instance().set_min_speed(s);
	if (lblMinSpeedVal)
		lblMinSpeedVal->setText(QString("%1%").arg(val));
}

void GamepadDock::onMaxSpeedChanged(int val)
{
	float s = (float)val / 100.0f;
	GamepadController::get_instance().set_max_speed(s);
	if (lblMaxSpeedVal)
		lblMaxSpeedVal->setText(QString("%1%").arg(val));
}

void GamepadDock::onZoomSpeedChanged(int val)
{
	float s = (float)val / 100.0f;
	GamepadController::get_instance().set_zoom_speed_mult(s);
	if (lblZoomSpeedVal)
		lblZoomSpeedVal->setText(QString("%1%").arg(val));
}

void GamepadDock::onDeadzoneChanged(int val)
{
	float d = (float)val / 100.0f;
	GamepadController::get_instance().set_deadzone(d);
	if (lblDeadzoneVal)
		lblDeadzoneVal->setText(QString("%1%").arg(val));
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

	int zoom_pct = (int)std::round(state.zoom_axis * 100.0f);
	triggerLeftBar->setValue((int)std::round(state.trigger_left * 100.0f));
	triggerRightBar->setValue((int)std::round(state.trigger_right * 100.0f));
	if (stickRightYBar) {
		stickRightYBar->setValue(zoom_pct);
		stickRightYBar->setFormat(QString("Zoom (Stick Dir): %1%").arg(zoom_pct));
	}

	int active_cam = GamepadController::get_instance().get_obsptz_active_device_id();
	std::string active_name = GamepadController::get_instance().get_obsptz_active_device_name();
	bool ptz_available = GamepadController::get_instance().is_ptz_available();
	if (cameraStatusLabel) {
		if (ptz_available) {
			cameraStatusLabel->setText(QString("🎥 PTZ: %1 (ID: %2) - Conectado")
							.arg(QString::fromUtf8(active_name.c_str()))
							.arg(active_cam));
			cameraStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #58a6ff; background: #161b22; padding: 4px 10px; border: 1px solid #30363d; border-radius: 4px;");
		} else {
			cameraStatusLabel->setText(obs_module_text("⚠️ PTZ: Plugin PTZ NÃO detectado! (Instalação Necessária)"));
			cameraStatusLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #ff7b72; background: #2d1515; padding: 4px 10px; border: 1px solid #f85149; border-radius: 4px;");
		}
	}

	// Atualiza Badges de Todos os 16 Botões Físicos
	lbl_btn_a->setStyleSheet(badge_style(state.btn_a));
	lbl_btn_b->setStyleSheet(badge_style(state.btn_b));
	lbl_btn_x->setStyleSheet(badge_style(state.btn_x));
	lbl_btn_y->setStyleSheet(badge_style(state.btn_y));
	lbl_btn_lb->setStyleSheet(badge_style(state.btn_lb));
	lbl_btn_rb->setStyleSheet(badge_style(state.btn_rb));
	lbl_btn_lt->setStyleSheet(badge_style(state.trigger_left > 0.4f || state.btn_lt));
	lbl_btn_rt->setStyleSheet(badge_style(state.trigger_right > 0.4f || state.btn_rt));
	lbl_dpad_up->setStyleSheet(badge_style(state.dpad_up));
	lbl_dpad_down->setStyleSheet(badge_style(state.dpad_down));
	lbl_dpad_left->setStyleSheet(badge_style(state.dpad_left));
	lbl_dpad_right->setStyleSheet(badge_style(state.dpad_right));
	lbl_btn_start->setStyleSheet(badge_style(state.btn_start));
	lbl_btn_back->setStyleSheet(badge_style(state.btn_back));
	lbl_btn_thumb_l->setStyleSheet(badge_style(state.btn_thumb_l));
	lbl_btn_thumb_r->setStyleSheet(badge_style(state.btn_thumb_r));

	if (lblRawInput) {
		std::string rawDesc = state.last_raw_input_desc;
		if (rawDesc.empty()) rawDesc = "Nenhuma";
		lblRawInput->setText(QString("📡 Entrada Física Bruta: %1").arg(QString::fromUtf8(rawDesc.c_str())));
	}

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
	obs_data_set_default_double(props, "ptz_curve_gamma", 2.2);
	obs_data_set_default_double(props, "ptz_min_speed", 0.04);
	obs_data_set_default_double(props, "ptz_max_speed", 1.0);
	obs_data_set_default_double(props, "ptz_zoom_speed", 0.8);
	obs_data_set_default_double(props, "ptz_deadzone", 0.12);

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
	obs_data_set_double(props, "ptz_curve_gamma", (double)GamepadController::get_instance().get_curve_gamma());
	obs_data_set_double(props, "ptz_min_speed", (double)GamepadController::get_instance().get_min_speed());
	obs_data_set_double(props, "ptz_max_speed", (double)GamepadController::get_instance().get_max_speed());
	obs_data_set_double(props, "ptz_zoom_speed", (double)GamepadController::get_instance().get_zoom_speed_mult());
	obs_data_set_double(props, "ptz_deadzone", (double)GamepadController::get_instance().get_deadzone());

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

	GamepadController::get_instance().save_profiles(props);
}

void GamepadDock::load_properties(obs_data_t *props)
{
	const char *selDev = obs_data_get_string(props, "selected_device");
	if (selDev && *selDev) {
		GamepadController::get_instance().set_selected_device(selDev);
	}

	float gamma = (float)obs_data_get_double(props, "ptz_curve_gamma");
	if (gamma < 0.5f) gamma = 2.2f;
	GamepadController::get_instance().set_curve_gamma(gamma);
	if (sliderCurve) sliderCurve->setValue((int)std::round(gamma * 10.0f));

	float min_spd = (float)obs_data_get_double(props, "ptz_min_speed");
	if (min_spd <= 0.0f) min_spd = 0.04f;
	GamepadController::get_instance().set_min_speed(min_spd);
	if (sliderMinSpeed) sliderMinSpeed->setValue((int)std::round(min_spd * 100.0f));

	float max_spd = (float)obs_data_get_double(props, "ptz_max_speed");
	if (max_spd <= 0.0f) max_spd = 1.0f;
	GamepadController::get_instance().set_max_speed(max_spd);
	if (sliderMaxSpeed) sliderMaxSpeed->setValue((int)std::round(max_spd * 100.0f));

	float zoom_spd = (float)obs_data_get_double(props, "ptz_zoom_speed");
	if (zoom_spd <= 0.0f) zoom_spd = 0.8f;
	GamepadController::get_instance().set_zoom_speed_mult(zoom_spd);
	if (sliderZoomSpeed) sliderZoomSpeed->setValue((int)std::round(zoom_spd * 100.0f));

	float dz = (float)obs_data_get_double(props, "ptz_deadzone");
	if (dz <= 0.0f) dz = 0.12f;
	GamepadController::get_instance().set_deadzone(dz);
	if (sliderDeadzone) sliderDeadzone->setValue((int)std::round(dz * 100.0f));

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

	GamepadController::get_instance().load_profiles(props);

	populateScenes();
	populateDevices();
	populateProfiles();
	updateRebindUI();
}

void GamepadDock::populateProfiles()
{
	if (!profileCombo) return;
	profileCombo->blockSignals(true);
	profileCombo->clear();

	const auto &profs = GamepadController::get_instance().get_profiles();
	std::string active = GamepadController::get_instance().get_active_profile_name();
	int selectIdx = 0;

	for (size_t i = 0; i < profs.size(); i++) {
		profileCombo->addItem(QString::fromUtf8(profs[i].name.c_str()));
		if (profs[i].name == active) {
			selectIdx = (int)i;
		}
	}

	profileCombo->setCurrentIndex(selectIdx);
	profileCombo->blockSignals(false);
}

void GamepadDock::updateRebindUI()
{
	GamepadCustomProfile &p = GamepadController::get_instance().get_active_profile();
	for (int i = 0; i < (int)VirtualAction::Count && i < (int)rebindRows.size(); i++) {
		if (rebindRows[i].lblCurrentBind) {
			if (!p.is_custom) {
				rebindRows[i].lblCurrentBind->setText("⚡ Automático (Nativo)");
				rebindRows[i].lblCurrentBind->setStyleSheet("color: #7ee787; font-family: monospace; font-size: 11px; background: #161b22; padding: 2px 6px; border-radius: 3px; border: 1px solid #30363d;");
			} else {
				const auto &b = p.bindings[i];
				if (b.type == BindingType::None) {
					rebindRows[i].lblCurrentBind->setText("Não Mapeado");
					rebindRows[i].lblCurrentBind->setStyleSheet("color: #8b949e; font-family: monospace; font-size: 11px; background: #161b22; padding: 2px 6px; border-radius: 3px; border: 1px solid #30363d;");
				} else {
					rebindRows[i].lblCurrentBind->setText(QString::fromUtf8(b.display_name.c_str()));
					rebindRows[i].lblCurrentBind->setStyleSheet("color: #58a6ff; font-weight: bold; font-family: monospace; font-size: 11px; background: #161b22; padding: 2px 6px; border-radius: 3px; border: 1px solid #30363d;");
				}
			}
		}
		if (rebindRows[i].btnMap) {
			rebindRows[i].btnMap->setText("🎯 Mapear");
			rebindRows[i].btnMap->setStyleSheet("padding: 3px 10px; font-size: 11px;");
		}
	}
}

void GamepadDock::onProfileSelected(int index)
{
	if (index < 0) return;
	QString name = profileCombo->itemText(index);
	GamepadController::get_instance().set_active_profile(name.toUtf8().constData());
	updateRebindUI();
}

void GamepadDock::onNewProfileClicked()
{
	bool ok = false;
	QString name = QInputDialog::getText(this, obs_module_text("Novo Perfil"),
	                                     obs_module_text("Nome do Perfil de Controle:"),
	                                     QLineEdit::Normal, "Meu Controle Customizado", &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	GamepadCustomProfile p;
	p.name = name.trimmed().toUtf8().constData();
	p.is_custom = true;
	p.device_guid = GamepadController::get_instance().get_active_device_guid();

	GamepadController::get_instance().add_or_update_profile(p);
	GamepadController::get_instance().set_active_profile(p.name);
	populateProfiles();
	updateRebindUI();
	if (lblRebindStatus)
		lblRebindStatus->setText(QString("Novo perfil criado: %1").arg(name));
}

void GamepadDock::onSaveProfileClicked()
{
	if (lblRebindStatus) {
		lblRebindStatus->setText(obs_module_text("✅ Perfil salvo com sucesso nas configurações do OBS!"));
	}
}

void GamepadDock::onDeleteProfileClicked()
{
	std::string cur = GamepadController::get_instance().get_active_profile_name();
	if (cur == "⚡ Automático (Padrão)" || cur == "Xbox Controller (Padrão)" || cur == "PlayStation (Padrão)") {
		if (lblRebindStatus)
			lblRebindStatus->setText(obs_module_text("⚠️ Não é possível excluir perfis padrão do sistema."));
		return;
	}

	GamepadController::get_instance().delete_profile(cur);
	populateProfiles();
	updateRebindUI();
	if (lblRebindStatus)
		lblRebindStatus->setText(obs_module_text("Perfil excluído."));
}

void GamepadDock::onResetXboxProfileClicked()
{
	GamepadCustomProfile &p = GamepadController::get_instance().get_active_profile();
	if (!p.is_custom) {
		GamepadCustomProfile xbox = GamepadController::create_default_xbox_profile();
		xbox.name = "Xbox Remapeado";
		GamepadController::get_instance().add_or_update_profile(xbox);
		GamepadController::get_instance().set_active_profile(xbox.name);
	} else {
		GamepadCustomProfile xbox = GamepadController::create_default_xbox_profile();
		for (int i = 0; i < (int)VirtualAction::Count; i++) {
			p.bindings[i] = xbox.bindings[i];
		}
		p.axis_pan = xbox.axis_pan;
		p.axis_tilt = xbox.axis_tilt;
		p.axis_zoom = xbox.axis_zoom;
	}
	populateProfiles();
	updateRebindUI();
	if (lblRebindStatus)
		lblRebindStatus->setText(obs_module_text("✅ Mapeamento padrão Xbox aplicado com sucesso!"));
}

void GamepadDock::onResetPlaystationProfileClicked()
{
	GamepadCustomProfile &p = GamepadController::get_instance().get_active_profile();
	if (!p.is_custom) {
		GamepadCustomProfile ps = GamepadController::create_default_playstation_profile();
		ps.name = "PlayStation Remapeado";
		GamepadController::get_instance().add_or_update_profile(ps);
		GamepadController::get_instance().set_active_profile(ps.name);
	} else {
		GamepadCustomProfile ps = GamepadController::create_default_playstation_profile();
		for (int i = 0; i < (int)VirtualAction::Count; i++) {
			p.bindings[i] = ps.bindings[i];
		}
		p.axis_pan = ps.axis_pan;
		p.axis_tilt = ps.axis_tilt;
		p.axis_zoom = ps.axis_zoom;
	}
	populateProfiles();
	updateRebindUI();
	if (lblRebindStatus)
		lblRebindStatus->setText(obs_module_text("✅ Mapeamento padrão PlayStation aplicado com sucesso!"));
}

void GamepadDock::onStartWizardClicked()
{
	isWizardActive = true;
	wizardStepIndex = 0;

	GamepadCustomProfile &p = GamepadController::get_instance().get_active_profile();
	if (!p.is_custom) {
		GamepadCustomProfile customP = GamepadController::create_default_xbox_profile();
		customP.name = "Controle Personalizado";
		GamepadController::get_instance().add_or_update_profile(customP);
		GamepadController::get_instance().set_active_profile(customP.name);
		populateProfiles();
	}

	advanceWizard();
}

void GamepadDock::advanceWizard()
{
	if (!isWizardActive) return;

	if (wizardStepIndex >= (int)VirtualAction::Count) {
		isWizardActive = false;
		if (lblRebindStatus)
			lblRebindStatus->setText(obs_module_text("🎉 Parabéns! Todos os 16 botões foram mapeados com sucesso!"));
		updateRebindUI();
		return;
	}

	int act = wizardStepIndex;
	if (lblRebindStatus) {
		lblRebindStatus->setText(QString("🧙 Passo %1 de 16: Pressione o botão para [%2] no controle...")
		                         .arg(act + 1)
		                         .arg(rebindRows[act].lblAction ? rebindRows[act].lblAction->text() : ""));
	}

	for (int i = 0; i < (int)rebindRows.size(); i++) {
		if (i == act) {
			rebindRows[i].btnMap->setText("👉 Aperte agora!");
			rebindRows[i].btnMap->setStyleSheet("background-color: #d29922; color: black; font-weight: bold; padding: 3px 10px; font-size: 11px;");
		} else {
			rebindRows[i].btnMap->setText("🎯 Mapear");
			rebindRows[i].btnMap->setStyleSheet("padding: 3px 10px; font-size: 11px;");
		}
	}

	GamepadController::get_instance().start_listening((VirtualAction)act, [this, act](VirtualAction action, const InputBinding &binding) {
		GamepadCustomProfile &prof = GamepadController::get_instance().get_active_profile();
		prof.bindings[(int)action] = binding;
		wizardStepIndex++;
		this->advanceWizard();
	});
}

void GamepadDock::onCancelListenClicked()
{
	isWizardActive = false;
	GamepadController::get_instance().cancel_listening();
	if (lblRebindStatus) {
		lblRebindStatus->setText(obs_module_text("⏹️ Escuta cancelada."));
	}
	updateRebindUI();
}

void GamepadDock::onSkipStepClicked()
{
	if (!isWizardActive) return;
	wizardStepIndex++;
	advanceWizard();
}

void GamepadDock::onRebindButtonClicked(int actionInt)
{
	if (actionInt < 0 || actionInt >= (int)VirtualAction::Count) return;

	isWizardActive = false;
	GamepadCustomProfile &p = GamepadController::get_instance().get_active_profile();
	if (!p.is_custom) {
		GamepadCustomProfile customP = GamepadController::create_default_xbox_profile();
		customP.name = "Controle Personalizado";
		GamepadController::get_instance().add_or_update_profile(customP);
		GamepadController::get_instance().set_active_profile(customP.name);
		populateProfiles();
	}

	for (int i = 0; i < (int)rebindRows.size(); i++) {
		if (i == actionInt) {
			rebindRows[i].btnMap->setText("👉 Aperte...");
			rebindRows[i].btnMap->setStyleSheet("background-color: #d29922; color: black; font-weight: bold; padding: 3px 10px; font-size: 11px;");
		} else {
			rebindRows[i].btnMap->setText("🎯 Mapear");
			rebindRows[i].btnMap->setStyleSheet("padding: 3px 10px; font-size: 11px;");
		}
	}

	if (lblRebindStatus) {
		lblRebindStatus->setText(QString("Aguardando entrada para [%1]... Pressione qualquer botão ou gatilho.")
		                         .arg(rebindRows[actionInt].lblAction ? rebindRows[actionInt].lblAction->text() : ""));
	}

	GamepadController::get_instance().start_listening((VirtualAction)actionInt, [this, actionInt](VirtualAction action, const InputBinding &binding) {
		GamepadCustomProfile &prof = GamepadController::get_instance().get_active_profile();
		prof.bindings[(int)action] = binding;
		if (this->lblRebindStatus) {
			this->lblRebindStatus->setText(QString("✅ [%1] mapeado para [%2]!")
			                               .arg(rebindRows[(int)action].lblAction ? rebindRows[(int)action].lblAction->text() : "")
			                               .arg(QString::fromUtf8(binding.display_name.c_str())));
		}
		this->updateRebindUI();
	});
}

void GamepadDock::onClearBindingClicked(int actionInt)
{
	if (actionInt < 0 || actionInt >= (int)VirtualAction::Count) return;

	GamepadCustomProfile &p = GamepadController::get_instance().get_active_profile();
	if (!p.is_custom) {
		GamepadCustomProfile customP = GamepadController::create_default_xbox_profile();
		customP.name = "Controle Personalizado";
		GamepadController::get_instance().add_or_update_profile(customP);
		GamepadController::get_instance().set_active_profile(customP.name);
		populateProfiles();
	}

	p.bindings[actionInt] = InputBinding();
	updateRebindUI();
	if (lblRebindStatus) {
		lblRebindStatus->setText(QString("Mapeamento de [%1] removido.")
		                         .arg(rebindRows[actionInt].lblAction ? rebindRows[actionInt].lblAction->text() : ""));
	}
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
