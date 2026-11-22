// App del editor de Titán: crear, programar y probar juegos en el propio teléfono.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "dev.titan.editor"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "dev.titan.editor"
        minSdk = 24
        targetSdk = 34
        versionCode = 11
        versionName = "0.12.0"
        ndk {
            abiFilters += ((project.findProperty("titanAbis") as String?) ?: "arm64-v8a,armeabi-v7a,x86_64").split(",")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DTITAN_BUILD_TESTS=OFF", "-DTITAN_BUILD_TOOLS=OFF")
                cppFlags += listOf("-fexceptions")
                targets += listOf("titan_editor_jni")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug") // clave de depuración: solo para pruebas
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].assets.srcDirs("../../engine/assets")
}
