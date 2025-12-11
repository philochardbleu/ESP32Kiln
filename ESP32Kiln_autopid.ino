
#include <PID_v1.h>
#include <sTune.h>

#define DEFAULT_TEMP_RISE_AFTER_OFF 30.0
#define CONTROL_HYSTERISIS .01

int measureInterval = 500;
int tuner_id = 5;
double tuner_noise_band = 1;
double tuner_output_step = 1;

float _calP = .5 / DEFAULT_TEMP_RISE_AFTER_OFF;
float _calD = 5.0 / DEFAULT_TEMP_RISE_AFTER_OFF;
float _calI = 4 / DEFAULT_TEMP_RISE_AFTER_OFF;

uint32_t settleTimeSec = 10;
uint32_t testTimeSec = 500;  // runPid interval = testTimeSec / samples
const uint16_t samples = 500;
const float inputSpan = 200;
const float outputSpan = 1000;
float outputStart = 0;
float outputStep = 50;
float tempLimit = 300;
uint8_t debounce = 0;
bool startup = true;

float Input, Output, Setpoint = 80, Kp, Ki, Kd;  // sTune

sTune tuner = sTune(&Input, &Output, tuner.ZN_PID, tuner.directIP, tuner.printOFF);

void CalibrateInit() {
  Program_run_state = PR_CALIBRATE;

  Output = 0;


#ifdef EMR_RELAY_PIN
  Enable_EMR();
#endif

  tuner.Configure(inputSpan, outputSpan, outputStart, outputStep, testTimeSec, settleTimeSec, samples);
  tuner.SetEmergencyStop(tempLimit);

  tuner.SetTuningMethod(tuner.ZN_PID);


  KilnPID.SetProportionalMode(KilnPID.pMode::pOnError);
  KilnPID.SetDerivativeMode(KilnPID.dMode::dOnError);
  KilnPID.SetAntiWindupMode(KilnPID.iAwMode::iAwClamp);

  Setpoint = Prefs[PRF_PID_AUTO_SETPOINT].value.vfloat;
  set_temp = Setpoint;
}

void HandleCalibration(unsigned long now) {
  float optimumOutput = tuner.softPwm(SSR1_RELAY_PIN, Input, Output, Setpoint, outputSpan, debounce);

  switch (tuner.Run()) {
    case tuner.sample:
      Update_TemperatureA();
      Input = kiln_temp;
      break;

    case tuner.tunings:                     // active just once when sTune is done
      tuner.GetAutoTunings(&Kp, &Ki, &Kd);  // sketch variables updated by sTune
      KilnPID.SetOutputLimits(0, outputSpan * 0.1);
      KilnPID.SetSampleTimeUs((outputSpan - 1) * 1000);
      debounce = 0;  // ssr mode
      Output = outputStep;
      KilnPID.SetTunings(Kp, Ki, Kd);  // update PID with the new tunings
      DBG dbgLog(LOG_INFO, "[AutoPID] Tuning complete Kp: %.2f \t Ki %.2f \t Kd = %.2f\n", Kp, Ki, Kd);
      Program_run_state = PR_ENDED;
      KilnPID.SetMode(KilnPID.Control::manual);
      Disable_SSR();

      Change_prefs_value(PrefsName[PRF_PID_KP], String(Kp));
      Change_prefs_value(PrefsName[PRF_PID_KI], String(Ki));
      Change_prefs_value(PrefsName[PRF_PID_KD], String(Kd));

      Save_prefs();
      break;

    case tuner.runPid:  // active once per sample after tunings
      if (startup && Input > Setpoint - 5) {
        startup = false;
        Output -= 9;
        KilnPID.SetMode(KilnPID.Control::manual);
        KilnPID.SetMode(KilnPID.Control::automatic);
      }
      Update_TemperatureA();
      Input = kiln_temp;
      KilnPID.Compute();
      Output = pid_out;
      break;
  }
}
