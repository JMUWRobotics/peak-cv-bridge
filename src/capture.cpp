#include "lib.hpp"

#include <bits/chrono.h>
#include <chrono>
#include <cxxopts.hpp>
#include <iomanip>
#include <iostream>
#include <opencv2/highgui.hpp>
#include <optional>

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

int
main(int argc, char** argv)
{
    bool trigger, auto_exposure, no_gui;
    double target_fps;
    std::optional<double> exposure_ms;
    int camera_index;

    cxxopts::Options desc(argv[0], "capture client for peakcvbridge");

    // clang-format off

    desc.add_options()
        ("h,help", "produce this message")
        ("c,camera", "Camera index", cxxopts::value<int>()->default_value("0"))
        ("t,trigger", "Enable trigger on Line0")
        ("f,framerate", "target fps", cxxopts::value<double>()->default_value("5.0"))
        ("a,auto-exposure", "Enable auto exposure")
        ("no-gui", "disable gui")
        ("e,exposure", "set exposure time in milliseconds. enabling auto-exposure will cause this to be ignored", cxxopts::value<double>());

    // clang-format on

    auto args = desc.parse(argc, argv);

    if (args.count("help")) {
        std::cout << desc.help() << '\n';
        return EXIT_SUCCESS;
    }

    camera_index = args["camera"].as<int>();
    trigger = args.count("trigger");
    target_fps = args["framerate"].as<double>();
    auto_exposure = args.count("auto-exposure");
    no_gui = args.count("no-gui");
    if (args.count("exposure"))
        exposure_ms = args["exposure"].as<double>();

    // without unique_ptr, PeakVideoCapture gets "sliced" into VideoCapture,
    // thus calling the wrong functions
    // https://stackoverflow.com/questions/1444025/c-overridden-method-not-getting-called
    std::unique_ptr<cv::VideoCapture> idsCap =
      std::make_unique<cv::PeakVideoCapture>();

    idsCap->setExceptionMode(true);

    try {
        idsCap->open(camera_index);
    } catch (const std::exception& e) {
        std::cerr << "Opening camera #" << camera_index << " failed:\n\t"
                  << e.what() << '\n';
        return 1;
    }

    idsCap->setExceptionMode(false);

    if (idsCap->set(cv::CAP_PROP_AUTO_EXPOSURE, auto_exposure))
        std::cout << (auto_exposure ? "Enabled" : "Disabled")
                  << " automatic exposure\n";

    if (!auto_exposure && exposure_ms.has_value() &&
        idsCap->set(cv::CAP_PROP_EXPOSURE, 1000. * exposure_ms.value()))
        std::cout << "Set exposure to " << exposure_ms.value() << " ms\n";

    if (idsCap->set(cv::CAP_PROP_FPS, target_fps))
        std::cout << "Set target framerate to " << target_fps << '\n';

    if (idsCap->set(cv::CAP_PROP_TRIGGER, trigger))
        std::cout << (trigger ? "Enabled" : "Disabled")
                  << " trigger on Line0\n";

    idsCap->setExceptionMode(true);

    if (!no_gui)
        cv::namedWindow("Stream", cv::WINDOW_KEEPRATIO);

    size_t framecount_total = 0, framecount_interval = 0;

    std::cout << std::setprecision(3);

    auto tick = system_clock::now();

    while (cv::pollKey() != 'q') {

        cv::Mat image;

        if (!idsCap->read(image))
            continue;

        auto tock = system_clock::now();

        ++framecount_interval;

        if (!no_gui)
            cv::imshow("Stream", image);

        std::cout << "\r[" << ++framecount_total << "]\t"
                  << idsCap->get(cv::CAP_PROP_EXPOSURE) / 1000. << " ms";
        if (!idsCap->get(cv::CAP_PROP_TRIGGER)) {
            std::cout << '\t' << idsCap->get(cv::CAP_PROP_FPS) << "FPS\t\t";
        } else if (1e6 <= duration_cast<microseconds>(tock - tick).count()) {
            tick = tock;
            std::cout << '\t' << framecount_interval << "FPS\t\t";
            framecount_interval = 0;
        }
        std::cout.flush();
    }

    std::cout << std::endl;
    idsCap->release();

    return 0;
}