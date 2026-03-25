This repository holds source code files for the ESP32-CAM, ESP32-DevKitC V4, and the '.zip' archive containing the FOMO object detection model.

<h1>FOMO Training Results:</h1>
<img width="1200" height="700" alt="FOMO_Training_Session_Two_Plot" src="https://github.com/user-attachments/assets/4a7a0758-360d-4d27-9ce2-68320c1e0ac1" />

<h1>(Tentative) Web Interface:</h1>
<img width="2560" height="836" alt="ESP32_CAM_Web_Interface" src="https://github.com/user-attachments/assets/10ecd6d0-f0cc-401c-9355-107110e5b368" />

<h1>Theory/Data</h1>

In order to measure the depth of a detected object, the focal length of the camera needs to be determined. We've done that empirically by measuring the width (in pixels) of the bounding box of a detected car at a predetermined length (10 feet) from the camera. The width of the car is 67 inches. Indeed, cars are generally 67 inches wide, which is a constant we use use alongside the focal length to determine the distance of detected cars. See the images below.

![IMG_0367](https://github.com/user-attachments/assets/2ed07acf-9bf4-48ce-9384-0dc215ac16dc)

![IMG_0368](https://github.com/user-attachments/assets/53a34a18-63a0-4b63-b083-fcdec43ed1f6)

![IMG_0369](https://github.com/user-attachments/assets/c379cd69-72d5-48a2-b176-9ab372f33e6d)

Owing to the fact that the ESP32-CAM lacks the necessary memory to run a better objection detection model, we were only able to detect cars at ranges of between between 5 and 10 feet. At 10 feet, the width of the bounding box was found to be 8 inches, and at 5 feet the width of the bounding box was found to be 16 inches. Taking the measurements at 10 feet to be the ground truth, the focal length was found to be approximately 14.3 pixels. Combing that with our initial assumption that cars are 67 inches wide, we can estimate distnace by taking the producting of those two constants and dividing them by the pixel width of any detected cars. If a detected car is 5 feet away from the user, then a photo will be taken and stored on the microSD card. (We'd originally wanted to build a stereoscopic camera, but because the resolution at which both ESP32-CAMs operate at is so low, attempting to estimate the distant of objects that are greater than a handful of inches away is not feasible.)

<img width="528" height="245" alt="ECE_397_Ground_Testing_Cropped_Highlighted" src="https://github.com/user-attachments/assets/741ecd8d-7e6b-49b8-ba7a-00bf90941752" />

<img width="528" height="245" alt="ECE_397_Ground_Testing_Five_Feet_Cropped_Highlighted" src="https://github.com/user-attachments/assets/9b4f316e-8556-493d-a630-48b6ed40f8bc" />

<img width="1646" height="1000" alt="Using_Similar_Triangles_To_Estimate_Focal_Length" src="https://github.com/user-attachments/assets/ad3b3727-569d-46c5-b368-9eb8baf8ae53" />
