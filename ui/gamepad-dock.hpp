#pragma once

#ifdef __cplusplus
#include <QFrame>
#include <obs.h>
#include <obs-frontend-api.h>
#include <string>

class QComboBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;
class QGroupBox;
class QSlider;

class GamepadDock : public QFrame {
	Q_OBJECT

public:
	GamepadDock(QWidget *parent = nullptr);
	~GamepadDock();

	static void default_properties(obs_data_t *props);
	void save_properties(obs_data_t *props);
	void load_properties(obs_data_t *props);
	void populateDevices();
	void populateProfiles();
	void updateRebindUI();

private slots:
	void onTimerUpdate();
	void onOpenBluetoothClicked();
	void onRefreshScenesClicked();
	void onSceneMappingChanged();
	void onDeviceSelected(int index);
	void onRefreshDevicesClicked();
	void onCurveGammaChanged(int val);
	void onMinSpeedChanged(int val);
	void onMaxSpeedChanged(int val);
	void onZoomSpeedChanged(int val);
	void onDeadzoneChanged(int val);

	// Mapeamento e Perfis Slots
	void onProfileSelected(int index);
	void onNewProfileClicked();
	void onSaveProfileClicked();
	void onDeleteProfileClicked();
	void onResetXboxProfileClicked();
	void onResetPlaystationProfileClicked();
	void onStartWizardClicked();
	void onCancelListenClicked();
	void onSkipStepClicked();
	void onRebindButtonClicked(int actionInt);
	void onClearBindingClicked(int actionInt);

private:
	QTimer *pollTimer;

	// Barra Superior
	QLabel *statusLabel;
	QComboBox *deviceCombo;
	QPushButton *refreshDevicesBtn;
	QLabel *cameraStatusLabel;
	QPushButton *bluetoothBtn;
	QPushButton *refreshScenesBtn;

	// Sliders de Curva e Sensibilidade PTZ
	QSlider *sliderCurve;
	QLabel *lblCurveVal;
	QSlider *sliderMinSpeed;
	QLabel *lblMinSpeedVal;
	QSlider *sliderMaxSpeed;
	QLabel *lblMaxSpeedVal;
	QSlider *sliderZoomSpeed;
	QLabel *lblZoomSpeedVal;
	QSlider *sliderDeadzone;
	QLabel *lblDeadzoneVal;

	// Mapeamentos de Cenas pelo D-Pad - Aba 1
	QComboBox *combo_dpad_up;
	QComboBox *combo_dpad_down;
	QComboBox *combo_dpad_left;
	QComboBox *combo_dpad_right;

	QComboBox *combo_rb_dpad_up;
	QComboBox *combo_rb_dpad_down;
	QComboBox *combo_rb_dpad_left;
	QComboBox *combo_rb_dpad_right;

	QComboBox *combo_rt_dpad_up;
	QComboBox *combo_rt_dpad_down;
	QComboBox *combo_rt_dpad_left;
	QComboBox *combo_rt_dpad_right;

	// Mapeamento e Perfis - Nova Aba
	QComboBox *profileCombo;
	QPushButton *btnNewProfile;
	QPushButton *btnSaveProfile;
	QPushButton *btnDeleteProfile;
	QPushButton *btnResetXbox;
	QPushButton *btnResetPS;
	QPushButton *btnWizard;
	QPushButton *btnCancelListen;
	QPushButton *btnSkipStep;
	QLabel *lblRebindStatus;

	struct RebindRowWidgets {
		QLabel *lblAction;
		QLabel *lblCurrentBind;
		QPushButton *btnMap;
		QPushButton *btnClear;
	};
	std::vector<RebindRowWidgets> rebindRows;

	int wizardStepIndex;
	bool isWizardActive;
	void advanceWizard();

	// Telemetria ao vivo - Aba Teste (16 botões completos + Sniffer Bruto)
	QLabel *stickLeftLabel;
	QProgressBar *stickLeftXBar;
	QProgressBar *stickLeftYBar;
	QProgressBar *stickRightYBar;
	QProgressBar *triggerLeftBar;
	QProgressBar *triggerRightBar;

	QLabel *lbl_btn_a;
	QLabel *lbl_btn_b;
	QLabel *lbl_btn_x;
	QLabel *lbl_btn_y;
	QLabel *lbl_btn_lb;
	QLabel *lbl_btn_rb;
	QLabel *lbl_btn_lt;
	QLabel *lbl_btn_rt;
	QLabel *lbl_btn_start;
	QLabel *lbl_btn_back;
	QLabel *lbl_btn_thumb_l;
	QLabel *lbl_btn_thumb_r;
	QLabel *lbl_dpad_up;
	QLabel *lbl_dpad_down;
	QLabel *lbl_dpad_left;
	QLabel *lbl_dpad_right;
	QLabel *lbl_mode;
	QLabel *lblRawInput;

	void populateScenes();
};

void gamepad_dock_init();
void gamepad_dock_release();

#endif
