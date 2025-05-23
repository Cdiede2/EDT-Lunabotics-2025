// Optimized for low latency and high performance on Jetson

#include <list>
#include <vector>
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <opencv2/opencv.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/cudafilters.hpp>

// #include <opencv2/imgproc.hpp>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/float32.hpp"
#include "sensor_msgs/msg/compressed_image.hpp"
#include "sensor_msgs/msg/image.hpp"

#include "librealsense2/rs.hpp"
// #include "SparkMax.hpp"

#define WEBCAM_ONE_PATH "/dev/video6"
#define WEBCAM_TWO_PATH "/dev/video8"

using namespace std::chrono_literals;

enum Cameras
{
  D455_ONE,
  D455_TWO,
  WEBCAM_ONE,
  WEBCAM_TWO
};

/**
 * @class MultiCameraNode
 * @brief Handles a single realsense camera (by serial ID) and two Web Cameras.
 *        Realsense camera provides both RGB and Depth frames.
 ******************************************************************************/
class MultiCameraNode : public rclcpp::Node
{
public:
  /**
   * @brief MultiCameraNode is the main constructor of the MultiCameraNode class.
   *        It initiallizes the activeCameras array, an array to keep track of connected cameras,
   *        and sets/creates pipelines to up to 4 cameras (2 RS and 2 Webcams). For those cameras
   *        which are connected, a publisher is initiallized. A timer with a frequency of 15Hz is created
   *        to publish video frames to all channels.
   * @return None
   * @exception No cameras detected (activeCameras is ALL false)
   */
  MultiCameraNode() : Node("multi_camera_node"), activeCameras({false, false, false, false})
  {
    // Camera Serial Numbers
    const std::string DEPTH_CAMERA_ONE_SERIAL = "318122303486";
    const std::string DEPTH_CAMERA_TWO_SERIAL = "308222300472";
    // this->activeCameras[0] = false;
    // this->activeCameras[1] = false;
    // this->activeCameras[2] = false;
    // this->activeCameras[3] = false;

    RCLCPP_INFO(this->get_logger(), "Multi-camera node startup.");

    // Known D455 serials
    rs2::config cfg_1;
    rs2::config cfg_2;

    spat_.set_option(RS2_OPTION_HOLES_FILL, 2);

    // Create Pipeline Configurations
    cfg_1.enable_device(DEPTH_CAMERA_ONE_SERIAL.c_str());
    cfg_1.enable_stream(RS2_STREAM_COLOR, 424, 240, RS2_FORMAT_BGR8, 15);
    cfg_1.enable_stream(RS2_STREAM_DEPTH, RS2_FORMAT_Z16, 15);

    cfg_2.enable_device(DEPTH_CAMERA_TWO_SERIAL.c_str());
    cfg_2.enable_stream(RS2_STREAM_COLOR, 424, 240, RS2_FORMAT_BGR8, 15);
    cfg_2.enable_stream(RS2_STREAM_DEPTH, 424, 240, RS2_FORMAT_Z16, 15);

    //////
    // Attempt to create pipeline to First D455 Camera
    RCLCPP_INFO(this->get_logger(), "Attempting to connect Pipeline 1");
    if (this->ctx.query_devices().size() > 0)
    {
      try
      {
        pipeline_1.start(cfg_1);
        this->activeCameras[Cameras::D455_ONE] = true;
      }
      catch (const rs2::error &exc)
      {
        RCLCPP_ERROR(this->get_logger(), "Error connecting to camera %s, ERROR: %s", DEPTH_CAMERA_ONE_SERIAL.c_str(), exc.what());
      }
    }
    else
    {
      RCLCPP_WARN(this->get_logger(), "D455 Camera One Not Connected");
    }

    //////
    // Attempt to create pipeline to Second D455 Camera
    RCLCPP_INFO(this->get_logger(), "Attempting to connect Pipeline 2");
    if (this->ctx.query_devices().size() > 1)
    {
      try
      {
        pipeline_2.start(cfg_2);
        this->activeCameras[Cameras::D455_TWO] = true;
      }
      catch (const rs2::error &exc)
      {
        RCLCPP_ERROR(this->get_logger(), "Error connecting to camera %s, ERROR: %s", DEPTH_CAMERA_TWO_SERIAL.c_str(), exc.what());
      }
    }
    else
    {
      RCLCPP_WARN(this->get_logger(), "D455 Camera Two Not Connected");
    }

    /////
    // Open Path to Webcam One, if possible
    RCLCPP_INFO(this->get_logger(), "Attempting to connect to Webcam 1");
    if (cap_rgb1_.open(WEBCAM_ONE_PATH))
    {
      cap_rgb1_.set(cv::CAP_PROP_FPS, 15);
      cap_rgb1_.set(cv::CAP_PROP_FRAME_WIDTH, 640);
      cap_rgb1_.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
      this->activeCameras[Cameras::WEBCAM_ONE] = true;
      RCLCPP_INFO(this->get_logger(), "Webcam at %s connected successfully", WEBCAM_ONE_PATH);
    }
    else
    {
      RCLCPP_WARN(this->get_logger(), "Webcam not found at %s", WEBCAM_ONE_PATH);
    }

    /////
    // Open Path to Webcam Two, if possible
    RCLCPP_INFO(this->get_logger(), "Attempting to connect to Webcam 2");
    if (cap_rgb2_.open(WEBCAM_TWO_PATH))
    {
      cap_rgb2_.set(cv::CAP_PROP_FPS, 15);
      cap_rgb2_.set(cv::CAP_PROP_FRAME_WIDTH, 640);
      cap_rgb2_.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
      this->activeCameras[Cameras::WEBCAM_TWO] = true;
      RCLCPP_INFO(this->get_logger(), "Webcam at %s connected successfully", WEBCAM_TWO_PATH);
    }
    else
    {
      RCLCPP_WARN(this->get_logger(), "Webcam not found at %s", WEBCAM_TWO_PATH);
    }

    /////
    // Create Publishers
    if (this->activeCameras[Cameras::D455_ONE])
    {
      RCLCPP_INFO(this->get_logger(), "Creating D455_1_ Publishers");
      d455_1_rgb_pub_ = this->create_publisher<sensor_msgs::msg::CompressedImage>("rs_node/camera1/compressed_video", 1);
      d455_1_dep_pub_ = this->create_publisher<sensor_msgs::msg::CompressedImage>("rs_node/camera1/depth_video", 1);
    }

    if (this->activeCameras[Cameras::D455_TWO])
    {
      RCLCPP_INFO(this->get_logger(), "Creating D455_2_ Publishers");
      d455_2_rgb_pub_ = this->create_publisher<sensor_msgs::msg::CompressedImage>("rs_node/camera2/compressed_video", 1);
      d455_2_dep_pub_ = this->create_publisher<sensor_msgs::msg::CompressedImage>("rs_node/camera2/depth_video", 1);
    }

    if (this->activeCameras[Cameras::WEBCAM_ONE])
    {
      RCLCPP_INFO(this->get_logger(), "Creating Webcam_1_ Publisher");
      rgb_cam1_pub_ = this->create_publisher<sensor_msgs::msg::CompressedImage>("rgb_cam1/compressed", 1);
    }

    if (this->activeCameras[Cameras::WEBCAM_TWO])
    {
      RCLCPP_INFO(this->get_logger(), "Creating Webcam_2_ Publisher");
      rgb_cam2_pub_ = this->create_publisher<sensor_msgs::msg::CompressedImage>("rgb_cam2/compressed", 1);
    }


    RCLCPP_WARN(this->get_logger(), "Camera 1 Status: %s", this->activeCameras[Cameras::D455_ONE]?"ON":"OFF");
    RCLCPP_WARN(this->get_logger(), "Camera 2 Status: %s", this->activeCameras[Cameras::D455_TWO]?"ON":"OFF");
    RCLCPP_WARN(this->get_logger(), "Camera 3 Status: %s", this->activeCameras[Cameras::WEBCAM_ONE]?"ON":"OFF");
    RCLCPP_WARN(this->get_logger(), "Camera 4 Status: %s", this->activeCameras[Cameras::WEBCAM_TWO]?"ON":"OFF");

    // No Cameras Connected
    if ( !this->activeCameras[Cameras::D455_ONE] &&
         !this->activeCameras[Cameras::D455_TWO] &&
         !this->activeCameras[Cameras::WEBCAM_ONE] &&
         !this->activeCameras[Cameras::WEBCAM_TWO])
    {

      // Throw Error, No cameras are detected
      RCLCPP_ERROR(this->get_logger(), "NO CAMERAS DETECTED");
      throw std::exception();
    }

    L_obstacle_detection_pub_ = this->create_publisher<std_msgs::msg::Bool>("obstacle_detection/left", 10);
    R_obstacle_detection_pub_ = this->create_publisher<std_msgs::msg::Bool>("obstacle_detection/right", 10);
    depth_detection_pub_ = this->create_publisher<std_msgs::msg::Float32>("depth_detection", 5);

    /////
    // Create Timer
    timer_ = this->create_wall_timer(66ms, std::bind(&MultiCameraNode::timer_callback, this)); // ~15 FPS
  }

private:
  rs2::context ctx;         // Global Context used to query devices
  rs2::pipeline pipeline_1; // Pipeline One used by the first D455 Camera
  rs2::pipeline pipeline_2; // Pipeline Two used by the second D455 Camera

  rs2::decimation_filter deci_;
  rs2::spatial_filter spat_;
  rs2::temporal_filter temp_;

  cv::VideoCapture cap_rgb1_; // VideoCapture stream used by the USB Webcams
  cv::VideoCapture cap_rgb2_; //

  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr L_obstacle_detection_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr R_obstacle_detection_pub_;
  rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr depth_detection_pub_;

  // D455 RGB Camera Publishers
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr d455_1_rgb_pub_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr d455_1_dep_pub_;

  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr d455_2_rgb_pub_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr d455_2_dep_pub_;

  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr edge_cam1_pub_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr rgb_cam1_pub_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr rgb_cam2_pub_;

  rclcpp::TimerBase::SharedPtr timer_; // Timer of ~15Hz, callback processes and publishes frames when and where available

  std::array<bool, 4> activeCameras; // Store the active cameras used by the class

  // cv::cuda::Filter filt;


  // TODO: Unfinished Function: processFrames 
  /**
   * @brief Retrieve Canny Edge detection of depth data and merge onto RGB image.
   * @param frames frameset containing a color frame and a depth frame
   * @returns cv::Mat object containing merged color image and Depth frame
   */
  /*
  cv::Mat processFrames(rs2::frameset &frames)
  {
    rs2::video_frame color_frame = frames.get_color_frame();
    rs2::depth_frame depth_frame = frames.get_depth_frame();

    cv::Mat depth_mat(cv::Size(depth_frame.get_width(), depth_frame.get_height()), CV_16UC1, (void *)depth_frame.get_data(), cv::Mat::AUTO_STEP);
    cv::Mat color_mat(cv::Size(color_frame.get_width(), color_frame.get_height()), CV_8UC3, (void *)color_frame.get_data(), cv::Mat::AUTO_STEP);

    cv::cuda::GpuMat src, dst;
    src.upload(depth_mat);

    filt = cv::cuda::createGaussianFilter(CV_16UC1, CV_16UC1, cv::Size(7,7), 3.0);
    filt.apply(src);

    cv::GaussianBlur(depth_mat, depth_mat, cv::Size(7,7), 3, 3 );
    return {};
  }
  */

  /**
   * @brief Callback to timer object. Callback polls the Realsense pipeline for available frames, avoiding locking the thread.
   *        If a frame is successfully polled, extracts both the depth and the RGB frmes. Publishes frames upon successful retrieval.
   * @param None
   * @return None
   *******************************************************/
  void timer_callback()
  {
    rs2::frameset frames;

    // If D455 One is Active AND a frame is available

    /////
    // D455 Camera One
    if (this->activeCameras[Cameras::D455_ONE])
    {
      if (pipeline_1.poll_for_frames(&frames))
      {
        // 1) Get raw frames
        rs2::video_frame color_fr = frames.get_color_frame();
        rs2::depth_frame depth_fr = frames.get_depth_frame();

        // 2) Publish color
        if (color_fr)
        {
          publish_realsense_image(color_fr, d455_1_rgb_pub_);
        }

        // 3) Filter & publish depth‐detection
        if (depth_fr && depth_fr.get_data())
        {
          rs2::frame f = depth_fr;
          f = spat_.process(f);
          f = temp_.process(f);
          auto filtered_depth = f.as<rs2::depth_frame>();

          (void)obstacle_detection_callback(filtered_depth);
        }
      }
      else
      {
        RCLCPP_WARN(this->get_logger(), "No D455 frames available.");
      }
    }

    /////
    // D455 Camera two
    if (this->activeCameras[Cameras::D455_TWO])
    {
      if (pipeline_2.poll_for_frames(&frames))
      {
        rs2::video_frame color_fr = frames.get_color_frame();
        rs2::depth_frame depth_fr = frames.get_depth_frame();

        // 2.) Publish Color
        if (color_fr)
        {
          publish_realsense_image(color_fr, d455_2_rgb_pub_);
        }

        // 3.) Filter and Publish Depth->Detection
        if (depth_fr && depth_fr.get_data())
        {
          rs2::frame f = depth_fr;
          f = spat_.process(f);
          f = temp_.process(f);
          auto filtered_depth = f.as<rs2::depth_frame>();
          (void)obstacle_detection_callback(filtered_depth);
        }
      }
      else
      {
        RCLCPP_WARN(this->get_logger(), "No D455 frames available.");
      }
    }

    // 4) Always publish available Webcams
    if (this->activeCameras[Cameras::WEBCAM_ONE])
    {
      publish_rgb_camera(cap_rgb1_, rgb_cam1_pub_);
    }
    if (this->activeCameras[Cameras::WEBCAM_TWO])
    {
      publish_rgb_camera(cap_rgb2_, rgb_cam2_pub_);
    }
  }

  /**
   * @brief Publishes a realsense frame to the passed publisher.
   * @param color_frame rs2::frame passed by reference. The color_frame to be sent.
   * @param pub a rclcpp::publisher to a Compressed Image.
   *******************************************************/
  void publish_realsense_image(rs2::frame &color_frame, rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr pub)
  {
    std::vector<uchar> buffer;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 40}; // Lower quality for faster encoding
    sensor_msgs::msg::CompressedImage msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "camera_rgb_optical_frame";
    msg.format = "jpeg";

    cv::Mat frame(cv::Size(color_frame.as<rs2::video_frame>().get_width(),
                           color_frame.as<rs2::video_frame>().get_height()),
                  CV_8UC3,
                  (void *)color_frame.get_data(),
                  cv::Mat::AUTO_STEP);
    if (!cv::imencode(".jpg", frame, buffer, params))
    {
      RCLCPP_ERROR(this->get_logger(), "Failed to encode RealSense image to JPEG.");
      return;
    }
    msg.data = buffer;
    pub->publish(msg);
  }

  /**
   * @brief Takes a cv::VideoCapture by reference and reads a frame. Frame is encoded and sent along message topic.
   * @param cap cv::VideoCapture reference object.
   * @param pub sensor_msgs::msg::CompressedImage.
   */
  void publish_rgb_camera(cv::VideoCapture &cap, rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr pub)
  {
    cv::Mat frame;
    if (!cap.read(frame))
    {
      RCLCPP_WARN(this->get_logger(), "Failed to capture frame from USB RGB camera.");
      return;
    }

    // Publish original image
    std::vector<uchar> buffer;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 40};
    sensor_msgs::msg::CompressedImage msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "rgb_camera_frame";
    msg.format = "jpeg";

    if (!cv::imencode(".jpg", frame, buffer, params))
    {
      RCLCPP_ERROR(this->get_logger(), "Failed to encode USB RGB image to JPEG.");
      return;
    }
    msg.data = buffer;
    pub->publish(msg);

    /**
    cv::Mat gray, edges, edges_bgr;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    // Improve contrast first
    cv::equalizeHist(gray, gray);

    // Adjust thresholds here if needed
    cv::Canny(gray, edges, 50, 150);

    // Colorize edges as red
    edges_bgr = cv::Mat::zeros(frame.size(), CV_8UC3);
    edges_bgr.setTo(cv::Scalar(0, 0, 255), edges);  // Red where edges exist

    // Encode edge image

    std::vector<uchar> edge_buffer;
    sensor_msgs::msg::CompressedImage edge_msg;
    edge_msg.header.stamp = this->now();
    edge_msg.header.frame_id = "rgb_camera_frame_edge";
    edge_msg.format = "jpeg";

    if (!cv::imencode(".jpg", edges_bgr, edge_buffer, params)) {
      RCLCPP_ERROR(this->get_logger(), "Failed to encode Canny edge image.");
      return;
    }

    edge_msg.data = edge_buffer;
    if (edge_cam1_pub_) {
      edge_cam1_pub_->publish(edge_msg);
      RCLCPP_INFO(this->get_logger(), "Published Canny edge image.");
    }
   */
  }

  /**
   * Function: average_depth
   * @brief Get the average depth pixel value across a rectangular region in the depth camera. Does not provide bounds checking.
   *
   * @param depth The depth frame passed by reference to the function call
   * @param x_bounds The lower and the upper bounds in the x-direction
   * @param y_bounds The lower and the upper bounds in the y-direction
   * @param increment The value to increment by between pixels
   * @returns double The average depth value across a region
   **********************************************************************/
  double average_depth(const rs2::depth_frame &depth, std::pair<int, int> x_bounds, std::pair<int, int> y_bounds, int increment = 1)
  {
    int numberPixels = (x_bounds.second - x_bounds.first) * (y_bounds.second - y_bounds.first) / (increment * increment);
    double average_depth = 0.0;

    for (int i = x_bounds.first; i <= x_bounds.second; i += increment)
    {
      for (int j = y_bounds.first; j <= y_bounds.second; j += increment)
      {
        average_depth += depth.get_distance(i, j);
      }
    }
    return average_depth / numberPixels;
  }

  /**
   * TODO: If possible, try to modularize the obstacle detection callback function.
   *  TODO: Aim to have the function length extend no further than 60 lines of code.
   */
  /**
   * @brief Obstacle detection callback function. Detects obstacles in the depth frame and publishes the results.
   * @param depth_frame The depth frame passed by reference to the function call
   * @param pub_left The publisher for the left side obstacle detection
   * @param pub_right The publisher for the right side obstacle detection
   * @param pub_depth The publisher for the average depth value
   * @returns None
   */
  void obstacle_detection_callback(const rs2::frame &depth_frame)
  {
    const int GROUND_DEPTH_MM = 700;
    const int POS_THRESHOLD = 250;
    const int NEG_THRESHOLD = 800;
    const int OBSTACLE_THRESHOLD = 200;

    const rs2::depth_frame depth = depth_frame.as<rs2::depth_frame>();
    const int width = depth.get_width();
    const int height = depth.get_height(); // 848 x 480

    int pos_count = 0;
    int neg_count = 0;
    int left_count = 0;
    int right_count = 0;

    float average_depth = 0.0;
    for (int y = 228; y <= 252; y += 4)
    {
      for (int x = 412; x <= 436; x += 4)
      {
        float dist_m = depth.get_distance(x, y);
        average_depth += dist_m;
      }
    }
    // double val = average_depth(depth_frame, {228, 252}, {412, 436}, 4);

    average_depth = average_depth / 49; // average of 49 pixels
    std_msgs::msg::Float32 depth_msg;

    depth_msg.data = average_depth;
    // depth_msg.data = average_depth(depth, {235,245}, {420, 428}, 1);

    depth_detection_pub_->publish(depth_msg);

    for (int y = height / 2; y < height; y += 5)
    {
      for (int x = 0; x < width / 2; x += 5)
      {
        float dist_m = depth.get_distance(x, y);
        if (dist_m <= 0.0f)
          continue;

        int depth_mm = static_cast<int>(dist_m * 1000);
        int delta = GROUND_DEPTH_MM - depth_mm;

        if (delta > POS_THRESHOLD)
        {
          pos_count++;
          right_count++;
        }
        else if (-delta > NEG_THRESHOLD)
        {
          neg_count++;
          right_count++;
        }
      }
    } // right side detection

    for (int y = height / 2; y < height; y += 5)
    {
      for (int x = width / 2; x < width; x += 5)
      {
        float dist_m = depth.get_distance(x, y);
        if (dist_m <= 0.0f)
          continue;

        int depth_mm = static_cast<int>(dist_m * 1000);
        int delta = GROUND_DEPTH_MM - depth_mm;

        if (delta > POS_THRESHOLD)
        {
          pos_count++;
          left_count++;
        }
        else if (-delta > NEG_THRESHOLD)
        {
          neg_count++;
          left_count++;
        }
      }
    } // left side detection

    std_msgs::msg::Bool left_msg;
    std_msgs::msg::Bool right_msg;

    if (pos_count > OBSTACLE_THRESHOLD)
    {
      right_msg.data = true;
      left_msg.data = false;
    }
    else if (neg_count > OBSTACLE_THRESHOLD)
    {
      right_msg.data = false;
      left_msg.data = true;
    }
    else
    {
      right_msg.data = false;
      left_msg.data = false;
    }

    // Publish Messages
    this->L_obstacle_detection_pub_->publish(left_msg);
    this->R_obstacle_detection_pub_->publish(right_msg);
  }
};

/**
 * @brief Entry point
 * @param argc Number of arguments
 * @param argv Arguments
 **********************************/
int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<MultiCameraNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}