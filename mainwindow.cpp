#include "mainwindow.h"
#include <QApplication>
#include <QScreen>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      currentPointIndex(0),
      inputFd(-1), // Initialize file descriptor to invalid
      inputNotifier(nullptr),
      currentRawX(0),
      currentRawY(0),
      hasX(false),
      hasY(false)
{
    QScreen *screen = QApplication::primaryScreen();

    setFixedSize(screen->availableSize().width(), screen->availableSize().height());

    // Setup UI
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    messageLabel = new QLabel("Tap the targets to calibrate the touchscreen.", centralWidget);
    messageLabel->setAlignment(Qt::AlignCenter);
    messageLabel->setStyleSheet("font-size: 24px; color: blue;");
    layout->addWidget(messageLabel);
    layout->addStretch(); // Push message label to top
    centralWidget->setLayout(layout);
    setCentralWidget(centralWidget);

    setupCalibrationPoints();
    displayMessage("Tap the first target.");

    // --- Raw Input Device Setup ---
    // IMPORTANT: You might need to change "/dev/input/event0" to match your touchscreen device.
    // Use `cat /proc/bus/input/devices` or `libinput list-devices` to find your device.
    // This application will likely need to be run with root privileges (sudo).
    const char* inputDevicePath = "/dev/input/touchscreen0";
    inputFd = open(inputDevicePath, O_RDONLY | O_NONBLOCK); // Open in non-blocking mode

    if (inputFd == -1) {
        qWarning() << "Failed to open input device:" << inputDevicePath << ". Error:" << strerror(errno);
        qWarning() << "Please ensure you have read permissions and the device path is correct.";
        qWarning() << "You might need to run this application as root (sudo).";
        displayMessage("Error: Could not open input device. Check console.");
    } else {
        qDebug() << "Successfully opened input device:" << inputDevicePath;
        // Create a QSocketNotifier to watch for data on the file descriptor
        inputNotifier = new QSocketNotifier(inputFd, QSocketNotifier::Read, this);
        connect(inputNotifier, &QSocketNotifier::activated, this, &MainWindow::readInputDevice);
    }
}

MainWindow::~MainWindow()
{
    if (inputFd != -1) {
        if (inputNotifier) {
            inputNotifier->setEnabled(false); // Disable notifier before closing FD
            delete inputNotifier; // Delete the notifier
        }
        ::close(inputFd); // Close the file descriptor
        qDebug() << "Closed input device file descriptor.";
    }
}

void MainWindow::setupCalibrationPoints()
{
    // Define 5 calibration points: Top-Left, Top-Right, Bottom-Right, Bottom-Left, Center
    // These are the *desired* screen coordinates.
    int margin = 50;
    targetPoints.append(QPoint(margin, margin));                               // 0: Top-Left
    targetPoints.append(QPoint(width() - margin, margin));                     // 1: Top-Right
    targetPoints.append(QPoint(width() - margin, height() - margin));          // 2: Bottom-Right
    targetPoints.append(QPoint(margin, height() - margin));                    // 3: Bottom-Left
    targetPoints.append(QPoint(width() / 2, height() / 2));                    // 4: Center

    // Initialize matrix to zeros
    for (int i = 0; i < 6; ++i) {
        matrix[i] = 0.0;
    }
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Fill background
    painter.fillRect(rect(), Qt::white);

    // Draw the current target point
    if (currentPointIndex < targetPoints.size()) {
        QPoint target = targetPoints[currentPointIndex];

        painter.setPen(QPen(Qt::red, 3));
        painter.setBrush(Qt::NoBrush);

        // Draw circle
        painter.drawEllipse(target, TARGET_RADIUS, TARGET_RADIUS);

        // Draw crosshair
        painter.drawLine(target.x() - CROSSHAIR_SIZE, target.y(), target.x() + CROSSHAIR_SIZE, target.y());
        painter.drawLine(target.x(), target.y() - CROSSHAIR_SIZE, target.x(), target.y() + CROSSHAIR_SIZE);

        painter.setPen(QPen(Qt::black, 1));
        painter.drawText(target.x() + TARGET_RADIUS + 5, target.y(), QString::number(currentPointIndex + 1));
    } else {
        // Calibration complete, show a message
        painter.setPen(QPen(Qt::black, 2));
        painter.setFont(QFont("Arial", 32));
        painter.drawText(rect(), Qt::AlignCenter, "Calibration Complete!");
    }
}

// This event handler is now primarily for non-touch events,
// as touch events are read directly from the input device.
bool MainWindow::event(QEvent *event)
{
    // We can still handle QMouseEvent here for desktop testing purposes
    // if the raw input device isn't available or configured.
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (currentPointIndex < targetPoints.size()) {
            qDebug() << "Mouse click detected (for fallback/testing):" << mouseEvent->pos();
            // You might want to remove this block if you strictly only want raw input.
            // For now, it provides a fallback if raw input setup is tricky.
            // actualTouchPoints.append(mouseEvent->pos());
            // currentPointIndex++;
            // if (currentPointIndex < targetPoints.size()) {
            //     displayMessage("Tap the next target.");
            // } else {
            //     displayMessage("Calculating calibration...");
            //     calculateTransformationMatrix();
            //     displayMessage("Calibration Complete! Check console for matrix.");
            // }
            // update();
            // return true;
        }
    }
    return QMainWindow::event(event); // Pass other events to base class
}

QPointF MainWindow::calculateMeanOfPoints(const std::vector<QPoint>& points) {
    // Check if the vector is empty to avoid division by zero.
    if (points.empty()) {
        qWarning("Input vector of points is empty. Returning QPointF(0.0, 0.0).");
        return QPointF(0.0, 0.0);
    }

    long long sumX = 0; // Use long long to prevent overflow for large number of points
    long long sumY = 0; // Use long long to prevent overflow for large number of points

    // Accumulate the sum of X and Y coordinates
    for (const QPoint& point : points) {
        sumX += point.x();
        sumY += point.y();
    }

    // Calculate the mean X and Y coordinates
    // Cast to double to ensure floating-point division
    double meanX = static_cast<double>(sumX) / points.size();
    double meanY = static_cast<double>(sumY) / points.size();

    return QPointF(meanX, meanY);
}

#define MAX_POINTS 10

void MainWindow::readInputDevice()
{
    struct input_event ev;
    ssize_t bytesRead;
    std::vector<QPoint> Points;
    bool pen_down = false;

    while ((bytesRead = read(inputFd, &ev, sizeof(ev))) == sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) {
                currentRawX = ev.value;
                hasX = true;
            } else if (ev.code == ABS_Y) {
                currentRawY = ev.value;
                hasY = true;
            }
            // For multi-touch, you might also look at ABS_MT_POSITION_X, ABS_MT_POSITION_Y
            // and ABS_MT_TRACKING_ID. For ts_calibrate, usually single touch is sufficient.
        } else if ((ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 1) || pen_down == true) {
            // SYN_REPORT indicates a complete event packet has been sent
            if (hasX && hasY) {
                pen_down = true;
                qDebug() << "Raw touch event received: X=" << currentRawX << "Y=" << currentRawY;

                Points.push_back(QPoint(currentRawX, currentRawX));

                if (Points.size() >= MAX_POINTS && currentPointIndex < targetPoints.size()) {
                    QPointF result = calculateMeanOfPoints(Points);
                    QPoint rawTouchPoint(result.x(), result.y());
                    Points.clear();
                    actualTouchPoints.append(rawTouchPoint);
                    qDebug() << "Recorded raw touch point:" << rawTouchPoint << "for target:" << targetPoints[currentPointIndex];
                    currentPointIndex++;

                    if (currentPointIndex < targetPoints.size()) {
                        displayMessage("Tap the next target.");
                    } else {
                        displayMessage("Calculating calibration...");
                        // Temporarily disable notifier to prevent new events while calculating
                        if (inputNotifier) inputNotifier->setEnabled(false);
                        calculateTransformationMatrix();
                        displayMessage("Calibration Complete! Check console for matrix.");
                    }
                    update(); // Redraw the window to show the next target or completion message
                }
                hasX = false;
                hasY = false;
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 0) {
            /* Clear the vector in case of release */
            pen_down = false;
            hasX = false;
            hasY = false;
            Points.clear();
        }
    }

    // Handle partial reads or errors (EAGAIN means no more data currently)
    if (bytesRead == -1 && errno != EAGAIN) {
        qWarning() << "Error reading from input device:" << strerror(errno);
        if (inputNotifier) inputNotifier->setEnabled(false); // Disable notifier on error
        displayMessage("Error reading input device. Check console.");
    }
}


// Helper function to invert a 3x3 matrix
// M_in: input 3x3 matrix
// M_out: output 3x3 inverse matrix
// Returns true on success, false if matrix is singular
bool MainWindow::invertMatrix3x3(const double M_in[3][3], double M_out[3][3])
{
    double det = M_in[0][0] * (M_in[1][1] * M_in[2][2] - M_in[2][1] * M_in[1][2]) -
                 M_in[0][1] * (M_in[1][0] * M_in[2][2] - M_in[2][0] * M_in[1][2]) +
                 M_in[0][2] * (M_in[1][0] * M_in[2][1] - M_in[2][0] * M_in[1][1]);

    if (qFuzzyIsNull(det)) {
        qWarning() << "Matrix is singular, cannot invert.";
        return false;
    }

    double invDet = 1.0 / det;

    M_out[0][0] = (M_in[1][1] * M_in[2][2] - M_in[2][1] * M_in[1][2]) * invDet;
    M_out[0][1] = (M_in[0][2] * M_in[2][1] - M_in[0][1] * M_in[2][2]) * invDet;
    M_out[0][2] = (M_in[0][1] * M_in[1][2] - M_in[0][2] * M_in[1][1]) * invDet;
    M_out[1][0] = (M_in[1][2] * M_in[2][0] - M_in[1][0] * M_in[2][2]) * invDet;
    M_out[1][1] = (M_in[0][0] * M_in[2][2] - M_in[0][2] * M_in[2][0]) * invDet;
    M_out[1][2] = (M_in[0][2] * M_in[1][0] - M_in[0][0] * M_in[1][2]) * invDet;
    M_out[2][0] = (M_in[1][0] * M_in[2][1] - M_in[2][0] * M_in[1][1]) * invDet;
    M_out[2][1] = (M_in[0][1] * M_in[2][0] - M_in[0][0] * M_in[2][1]) * invDet;
    M_out[2][2] = (M_in[0][0] * M_in[1][1] - M_in[0][1] * M_in[1][0]) * invDet;

    return true;
}

void MainWindow::calculateTransformationMatrix()
{
    // This implements a least-squares solution for the 6 parameters (A-F)
    // of the affine transformation:
    // x_screen = A * x_raw + B * y_raw + C
    // y_screen = D * x_raw + E * y_raw + F
    //
    // We set up two independent systems of linear equations (one for X, one for Y)
    // and solve them using the normal equations method: (X^T * X) * beta = X^T * Y
    // where beta = [A, B, C]^T or [D, E, F]^T

    if (actualTouchPoints.size() < 3) {
        qWarning() << "Not enough points to calculate 6-parameter affine matrix (at least 3 required)!";
        return;
    }

    int N = actualTouchPoints.size();

    // Calculate sums for the (X^T * X) matrix (same for both X and Y calculations)
    double S_xx = 0.0, S_xy = 0.0, S_x = 0.0;
    double S_yy = 0.0, S_y = 0.0;
    double S_1 = (double)N; // Sum of 1s is just the number of points

    for (int i = 0; i < N; ++i) {
        double x_raw = actualTouchPoints[i].x();
        double y_raw = actualTouchPoints[i].y();

        S_xx += x_raw * x_raw;
        S_xy += x_raw * y_raw;
        S_x  += x_raw;
        S_yy += y_raw * y_raw;
        S_y  += y_raw;
    }

    // Form the (X^T * X) matrix (M_design)
    double M_design[3][3] = {
        {S_xx, S_xy, S_x},
        {S_xy, S_yy, S_y},
        {S_x,  S_y,  S_1}
    };

    double M_design_inv[3][3];
    if (!invertMatrix3x3(M_design, M_design_inv)) {
        qWarning() << "Could not invert design matrix. Calibration failed.";
        return;
    }

    // --- Solve for A, B, C (using target X coordinates) ---
    double R_x_vec[3] = {0.0, 0.0, 0.0}; // Right-hand side vector for X

    for (int i = 0; i < N; ++i) {
        double x_raw = actualTouchPoints[i].x();
        double y_raw = actualTouchPoints[i].y();
        double x_screen = targetPoints[i].x();

        R_x_vec[0] += x_raw * x_screen;
        R_x_vec[1] += y_raw * x_screen;
        R_x_vec[2] += x_screen;
    }

    // Calculate A, B, C: beta_x = M_design_inv * R_x_vec
    matrix[0] = M_design_inv[0][0] * R_x_vec[0] + M_design_inv[0][1] * R_x_vec[1] + M_design_inv[0][2] * R_x_vec[2]; // A
    matrix[1] = M_design_inv[1][0] * R_x_vec[0] + M_design_inv[1][1] * R_x_vec[1] + M_design_inv[1][2] * R_x_vec[2]; // B
    matrix[2] = M_design_inv[2][0] * R_x_vec[0] + M_design_inv[2][1] * R_x_vec[1] + M_design_inv[2][2] * R_x_vec[2]; // C

    // --- Solve for D, E, F (using target Y coordinates) ---
    double R_y_vec[3] = {0.0, 0.0, 0.0}; // Right-hand side vector for Y

    for (int i = 0; i < N; ++i) {
        double x_raw = actualTouchPoints[i].x();
        double y_raw = actualTouchPoints[i].y();
        double y_screen = targetPoints[i].y();

        R_y_vec[0] += x_raw * y_screen;
        R_y_vec[1] += y_raw * y_screen;
        R_y_vec[2] += y_screen;
    }

    // Calculate D, E, F: beta_y = M_design_inv * R_y_vec
    matrix[3] = M_design_inv[0][0] * R_y_vec[0] + M_design_inv[0][1] * R_y_vec[1] + M_design_inv[0][2] * R_y_vec[2]; // D
    matrix[4] = M_design_inv[1][0] * R_y_vec[0] + M_design_inv[1][1] * R_y_vec[1] + M_design_inv[1][2] * R_y_vec[2]; // E
    matrix[5] = M_design_inv[2][0] * R_y_vec[0] + M_design_inv[2][1] * R_y_vec[1] + M_design_inv[2][2] * R_y_vec[2]; // F

    qDebug() << "Calculated Transformation Matrix (A, B, C, D, E, F):";
    qDebug() << "A =" << matrix[0];
    qDebug() << "B =" << matrix[1];
    qDebug() << "C =" << matrix[2];
    qDebug() << "D =" << matrix[3];
    qDebug() << "E =" << matrix[4];
    qDebug() << "F =" << matrix[5];

    // Example of how to use the matrix to transform a raw point:
    // We'll test with all points and show the average error.
    double totalErrorX = 0.0;
    double totalErrorY = 0.0;

    qDebug() << "\nTesting transformation with all points:";
    for (int i = 0; i < N; ++i) {
        QPoint testRawPoint = actualTouchPoints[i];
        QPoint testScreenPoint = targetPoints[i];

        double transformedX = matrix[0] * testRawPoint.x() + matrix[1] * testRawPoint.y() + matrix[2];
        double transformedY = matrix[3] * testRawPoint.x() + matrix[4] * testRawPoint.y() + matrix[5];

        qDebug() << "Point " << i + 1 << ": Raw=" << testRawPoint << ", Expected=" << testScreenPoint
                 << ", Transformed=" << QPointF(transformedX, transformedY)
                 << ", Diff=(" << transformedX - testScreenPoint.x() << "," << transformedY - testScreenPoint.y() << ")";

        totalErrorX += std::abs(transformedX - testScreenPoint.x());
        totalErrorY += std::abs(transformedY - testScreenPoint.y());
    }

    qDebug() << "Average absolute error X:" << totalErrorX / N;
    qDebug() << "Average absolute error Y:" << totalErrorY / N;

    // In a real ts_calibrate, these values (A, B, C, D, E, F) would be written to
    // a configuration file, e.g., /etc/pointercal, in a specific format.
    // The format typically looks like: A B C D E F G (where G is 1 or 65536)
    // Here, we are just printing the raw double values.
    // The 7th parameter 'G' is typically 65536 for integer-based fixed-point math.
    qDebug() << "\nSimulated /etc/pointercal line (scaled by 65536 for integer format):";
    qDebug() << qRound(matrix[0] * 65536) << qRound(matrix[1] * 65536) << qRound(matrix[2] * 65536)
             << qRound(matrix[3] * 65536) << qRound(matrix[4] * 65536) << qRound(matrix[5] * 65536) << 65536;
}

void MainWindow::displayMessage(const QString &message)
{
    messageLabel->setText(message);
}
