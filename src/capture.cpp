#include "lib.hpp"

#include <bits/chrono.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <optional>
#include <stdexcept>

#include <cxxopts.hpp>
#include <fmt/core.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

using namespace std::chrono;

namespace std {
template<typename T>
std::ostream&
operator<<(std::ostream& os, const std::optional<T>& opt)
{
    if (opt.has_value()) {
        os << opt.value();
    } else {
        os << "(nullopt)";
    }
    return os;
}
template<typename T>
std::istream&
operator>>(std::istream& is, std::optional<T>& opt)
{
    T value;
    if (is >> value) {
        opt = std::move(value);
    } else {
        opt.reset();
    }
    return is;
}
}

void
to_v4l(const cv::Mat& _img, int fd)
{
    static bool had_ioctl = false;
    static size_t size_image;

    static cv::Mat yuyv;
    {
        static cv::Mat color;
        if (_img.channels() == 3)
            color = _img;
        else
            cv::cvtColor(_img, color, cv::COLOR_GRAY2BGR);

        cv::cvtColor(color, yuyv, cv::COLOR_BGR2YUV_YUYV);
    }

    if (!had_ioctl) {
        // clang-format off
        struct v4l2_format fmt = {
            .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
            .fmt = {
                .pix = {
                    .width = (__u32)yuyv.cols,
                    .height = (__u32)yuyv.rows,
                    .pixelformat = V4L2_PIX_FMT_YUYV,
                    .field = V4L2_FIELD_NONE,
                    .sizeimage = (__u32)(size_image = yuyv.size().area() * yuyv.elemSize()),
                },
            }
        };

        if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
            throw std::runtime_error(
              fmt::format("ioctl failed: {}", strerror(errno)));

        // clang-format on

        had_ioctl = true;
    }

    if (write(fd, yuyv.data, size_image) != size_image)
        fmt::println(stderr, "write failed: {}", strerror(errno));
}

static bool ctrlc = false;

int
main(int argc, char** argv)
{
    bool trigger, auto_exposure, is_v4l;
    double target_fps;
    std::optional<double> exposure_ms;
    int camera_index, v4l_fd = -1;

    cxxopts::Options desc(argv[0], "capture client for peakcvbridge");

    // clang-format off

    desc.add_options()
        ("h,help", "produce this message")
        ("c,camera", "camera index", cxxopts::value<int>()->default_value("0"))
        ("t,trigger", "enable trigger on Line0")
        ("f,framerate", "target fps", cxxopts::value<double>()->default_value("30.0"))
        ("a,auto-exposure", "enable auto exposure")
        ("v,v4l2loopback", "write to v4ltoloopback device", cxxopts::value<std::string>()->implicit_value("/dev/video0"))
        ("e,exposure", "set exposure time in milliseconds. enabling auto-exposure will cause this to be ignored", cxxopts::value<double>());

    // clang-format on

    auto args = desc.parse(argc, argv);

    if (args.count("help")) {
        fmt::println("{}", desc.help());
        return EXIT_SUCCESS;
    }

    camera_index = args["camera"].as<int>();
    trigger = args.count("trigger");
    target_fps = args["framerate"].as<double>();
    auto_exposure = args.count("auto-exposure");
    if ((is_v4l = args.count("v4l2loopback"))) {
        auto devpath = args["v4l2loopback"].as<std::string>();
        v4l_fd = open(devpath.c_str(), O_WRONLY, 0);
        if (v4l_fd < 0)
            std::invalid_argument(
              fmt::format("cannot open {}: {}", devpath, strerror(errno)));
    }
    if (args.count("exposure"))
        exposure_ms = args["exposure"].as<double>();

    // without unique_ptr, PeakVideoCapture gets "sliced" into VideoCapture,
    // thus calling the wrong functions
    // https://stackoverflow.com/questions/1444025/c-overridden-method-not-getting-called
    auto idsCap = std::make_unique<cv::PeakVideoCapture>(true);

    idsCap->setExceptionMode(true);

    try {
        idsCap->open(camera_index);
    } catch (const std::exception& e) {
        fmt::println(stderr, "Opening camera #{} failed:", camera_index);
        fmt::println(stderr, "\t{}", e.what());
        return 1;
    }

    idsCap->setExceptionMode(false);

    if (idsCap->set(cv::CAP_PROP_AUTO_EXPOSURE, auto_exposure))
        fmt::println("{} automatic exposure",
                     auto_exposure ? "Enabled" : "Disabled");

    if (!auto_exposure && exposure_ms.has_value() &&
        idsCap->set(cv::CAP_PROP_EXPOSURE, 1000. * exposure_ms.value()))
        fmt::println("Set exposure to {} ms", exposure_ms.value());

    if (idsCap->set(cv::CAP_PROP_FPS, target_fps))
        fmt::println("Set target framerate to {}", target_fps);

    if (idsCap->set(cv::CAP_PROP_TRIGGER, trigger))
        fmt::println("{} trigger on Line0", trigger ? "Enabled" : "Disabled");

    idsCap->setExceptionMode(true);

    if (!args.count("v4l2loopback"))
        cv::namedWindow("Stream", cv::WINDOW_KEEPRATIO);

    {
        size_t framecount_total = 0, framecount_interval = 0;
        auto tick = system_clock::now();
        double fps = 0.0;

        std::function<bool(void)> poll;
        if (is_v4l)
            poll = []() { return true; };
        else
            poll = []() { return cv::pollKey() != 'q'; };

        signal(SIGINT, [](int sig) {
            fmt::println(
              stderr, "\nCaught signal: {} ({})", sig, strsignal(sig));
            ctrlc = true;
        });

        while (!ctrlc && poll()) {

            cv::Mat image;

            if (!idsCap->read(image))
                continue;

            auto tock = system_clock::now();

            if (!is_v4l)
                cv::imshow("Stream", image);
            else
                to_v4l(image, v4l_fd);

            if (isatty(STDOUT_FILENO)) {

                ++framecount_interval;

                if (!idsCap->get(cv::CAP_PROP_TRIGGER)) {
                    fps = idsCap->get(cv::CAP_PROP_FPS);
                } else if (1e6 <=
                           duration_cast<microseconds>(tock - tick).count()) {
                    tick = tock;
                    fps = framecount_interval;
                    framecount_interval = 0;
                }

                fmt::print("\r[{}]\t{:.3f} ms\t{:.3f} FPS\t\t",
                           ++framecount_total,
                           idsCap->get(cv::CAP_PROP_EXPOSURE) / 1000.,
                           fps);
                fflush(stdout);
            }
        }
    }

    idsCap->release();
    close(v4l_fd);

    return 0;
}