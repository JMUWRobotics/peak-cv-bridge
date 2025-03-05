#include "genicvbridge.hpp"

#ifdef BRIDGE_V4L2LOOPBACK
#include <fcntl.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <chrono>
#include <optional>

#include <cxxopts.hpp>
#include <fmt/core.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

using namespace std::chrono;
using namespace XVII;

#ifdef BRIDGE_V4L2LOOPBACK
void
to_v4l(const cv::Mat& _img, int fd)
{
    static bool had_ioctl = false;
    static ssize_t size_image;

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
#endif

static bool ctrlc = false;

int
main(int argc, char** argv)
{
    bool trigger, auto_exposure;
    double target_fps;
    std::optional<double> exposure_ms;
    int camera_index;
#ifdef BRIDGE_V4L2LOOPBACK
    bool is_v4l;
    int v4l_fd = -1;
#endif

    cxxopts::Options desc(argv[0], "capture client for peakcvbridge");

    // clang-format off

    desc.add_options()
        ("h,help", "produce this message")
        ("c,camera", "camera index", cxxopts::value<int>()->default_value("0"))
        // ("b,backend", "camera backend", cxxopts::value<GenICamVideoCapture::Backend>())
        ("t,trigger", "enable trigger on Line0")
        ("f,framerate", "target fps", cxxopts::value<double>()->default_value("30.0"))
        ("a,auto-exposure", "enable auto exposure")
#ifdef BRIDGE_V4L2LOOPBACK
        ("v,v4l2loopback", "write to v4ltoloopback device", cxxopts::value<std::string>()->implicit_value("/dev/video0"))
#endif
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
#ifdef BRIDGE_V4L2LOOPBACK
    if ((is_v4l = args.count("v4l2loopback"))) {
        auto devpath = args["v4l2loopback"].as<std::string>();
        v4l_fd = open(devpath.c_str(), O_WRONLY, 0);
        if (v4l_fd < 0)
            std::invalid_argument(
              fmt::format("cannot open {}: {}", devpath, strerror(errno)));
    }
#endif
    if (args.count("exposure"))
        exposure_ms = args["exposure"].as<double>();

    // without unique_ptr, PeakVideoCapture gets "sliced" into VideoCapture,
    // thus calling the wrong functions
    // https://stackoverflow.com/questions/1444025/c-overridden-method-not-getting-called
    auto camera = std::make_unique<GenICamVideoCapture>(true);

    camera->setExceptionMode(true);

    try {
        camera->open(camera_index,
                     static_cast<int>(GenICamVideoCapture::Backend::IDS_PEAK));
    } catch (const std::exception& e) {
        fmt::println(stderr, "Opening camera #{} failed:", camera_index);
        fmt::println(stderr, "\t{}", e.what());
        return 1;
    }

    camera->setExceptionMode(false);

    if (camera->set(cv::CAP_PROP_AUTO_EXPOSURE, auto_exposure))
        fmt::println("{} automatic exposure",
                     auto_exposure ? "Enabled" : "Disabled");

    if (!auto_exposure && exposure_ms.has_value() &&
        camera->set(cv::CAP_PROP_EXPOSURE, 1000. * exposure_ms.value()))
        fmt::println("Set exposure to {} ms", exposure_ms.value());

    if (camera->set(cv::CAP_PROP_FPS, target_fps))
        fmt::println("Set target framerate to {}", target_fps);

    if (camera->set(cv::CAP_PROP_TRIGGER, trigger))
        fmt::println("{} trigger on Line0", trigger ? "Enabled" : "Disabled");

    camera->setExceptionMode(true);

    if (!args.count("v4l2loopback"))
        cv::namedWindow("Stream", cv::WINDOW_KEEPRATIO);

    {
        size_t framecount_total = 0, framecount_interval = 0;
        auto tick = system_clock::now();
        double fps = 0.0;

        std::function<bool(void)> poll;
#ifdef BRIDGE_V4L2LOOPBACK
        if (is_v4l)
            poll = []() { return true; };
        else
#endif
            poll = []() { return cv::pollKey() != 'q'; };

        signal(SIGINT, [](int sig) {
            fmt::println(
              stderr, "\nCaught signal: {} ({})", sig, strsignal(sig));
            ctrlc = true;
        });

        while (poll() && !ctrlc) {

            cv::Mat image;

            if (!camera->read(image))
                continue;

            auto tock = system_clock::now();

#ifdef BRIDGE_V4L2LOOPBACK
            if (is_v4l)
                to_v4l(image, v4l_fd);
            else
#endif
                cv::imshow("Stream", image);

            if (isatty(STDOUT_FILENO) && !ctrlc) {

                ++framecount_interval;

                if (!camera->get(cv::CAP_PROP_TRIGGER)) {
                    fps = camera->get(cv::CAP_PROP_FPS);
                } else if (1e6 <=
                           duration_cast<microseconds>(tock - tick).count()) {
                    tick = tock;
                    fps = framecount_interval;
                    framecount_interval = 0;
                }

                fmt::print("\r[{}]\t{:.3f} ms\t{:.3f} FPS\t\t",
                           ++framecount_total,
                           camera->get(cv::CAP_PROP_EXPOSURE) / 1000.,
                           fps);
                fflush(stdout);
            }
        }
    }

    camera->release();
#ifdef BRIDGE_V4L2LOOPBACK
    close(v4l_fd);
#endif

    return 0;
}