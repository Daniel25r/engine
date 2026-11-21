plugins {
    id("com.android.application")
}

android {
    namespace = "dev.forja.demo"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "dev.forja.demo"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Firmado con la clave de depuración para poder instalarlo ya.
            // Para publicar en Google Play hay que usar una clave propia.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].assets.srcDirs("../../engine/assets", "../../sample/assets")
}
