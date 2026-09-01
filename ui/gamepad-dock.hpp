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

class GamepadDock : public QFrame {
	Q_OBJECT

public:
	GamepadDock(QWidget *parent = nullptr);
	~GamepadDock();

	static void default_properties(obs_data_t *props);
	void save_properties(obs_data_t *props);
	void load_properties(obs_data_t *props);

private slots:
	void onTimerUpdate();
	void onOpenBluetoothClicked();
	void onRefreshScenesClicked();
	void onSceneMappingChanged();

private:
	QTimer *pollTimer;

	// Barra Superior
	QLabel *statusLabel;
	QPushButton *bluetoothBtn;
	QPushButton *refreshScenesBtn;

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

	// Telemetria ao vivo - Aba 2
	QLabel *stickLeftLabel;
	QProgressBar *stickLeftXBar;
	QProgressBar *stickLeftYBar;
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
	QLabel *lbl_dpad;
	QLabel *lbl_mode;

	void populateScenes();
};

void gamepad_dock_init();
void gamepad_dock_release();

#endif
