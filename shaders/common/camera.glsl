layout(set = 0, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 params;   // x: time(s), y: aspect, zw: reserved
} camera;
