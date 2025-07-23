#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QPoint>
#include <QPaintEvent>
#include <QMouseEvent> // Still useful for desktop testing/debugging if raw input fails
#include <QPainter>
#include <QDebug>
#include <QLabel>
#include <QVBoxLayout>
#include <cmath> // For qFuzzyIsNull, std::abs

// For raw input device access
#include <QFile>
#include <QSocketNotifier>
#include <fcntl.h> // For open()
#include <unistd.h> // For close()
#include <linux/input.h> // For input_event struct

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent *event) override;
    // We'll still override event() for general Qt events, but touch handling moves to readInputDevice()
    bool event(QEvent *event) override;
    QPointF calculateMeanOfPoints(const std::vector<QPoint, std::allocator<QPoint> >&);

private slots:
    void readInputDevice(); // New slot to read from the input device

private:
    void setupCalibrationPoints();
    void calculateTransformationMatrix();
    void displayMessage(const QString &message);

    // Helper function for 3x3 matrix inversion
    bool invertMatrix3x3(const double M_in[3][3], double M_out[3][3]);

    QVector<QPoint> targetPoints; // Desired screen coordinates for calibration
    QVector<QPoint> actualTouchPoints; // Actual raw touch coordinates recorded by user
    int currentPointIndex;
    QLabel *messageLabel;

    // The 6 parameters of the affine transformation matrix
    // x_screen = A * x_raw + B * y_raw + C
    // y_screen = D * x_raw + E * y_raw + F
    double matrix[6]; // A, B, C, D, E, F

    const int TARGET_RADIUS = 20; // Radius for drawing the target circle
    const int CROSSHAIR_SIZE = 10; // Size for the crosshair lines

    // Raw input device members
    int inputFd; // File descriptor for the input device
    QSocketNotifier *inputNotifier; // Notifier for raw input events
    int currentRawX; // To store current raw X from input_event
    int currentRawY; // To store current raw Y from input_event
    bool hasX; // Flag to indicate if X coordinate has been read for current event
    bool hasY; // Flag to indicate if Y coordinate has been read for current event
};

#endif // MAINWINDOW_H
