// Plantilla de exportación Android de Titán.
// Empaqueta el runtime nativo (titan_player) con un paquete de juego generado por "titan package".
//
//   ./gradlew assembleRelease -PtitanPackage=/ruta/al/paquete \
//       -PtitanAppId=com.estudio.juego -PtitanAppName="Mi juego" -PtitanVersionName=1.0 -PtitanVersionCode=1
plugins {
    id("com.android.application")
}

fun prop(name: String, default: String) = (project.findProperty(name) as String?) ?: default

val gamePackage = file(prop("titanPackage", "../../samples/demo/package"))
if (!gamePackage.resolve("project.titan").exists()) {
    logger.warn("Titán: no hay un paquete de juego en $gamePackage. Genera uno con: titan package <proyecto> <salida>")
}

android {
    namespace = "dev.titan.player"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = prop("titanAppId", "dev.titan.demo")
        minSdk = 24
        targetSdk = 34
        versionCode = prop("titanVersionCode", "1").toInt()
        versionName = prop("titanVersionName", "0.2.0")
        resValue("string", "app_name", prop("titanAppName", "Titán Demo"))
        ndk {
            abiFilters += prop("titanAbis", "arm64-v8a,armeabi-v7a,x86_64").split(",")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DTITAN_BUILD_TESTS=OFF", "-DTITAN_BUILD_TOOLS=OFF")
                cppFlags += listOf("-fexceptions")
                targets += listOf("titan_player")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Sin clave propia configurada se firma con la clave de depuración (solo para pruebas).
            // La firma de publicación se configurará sin guardar claves en el repositorio.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].assets.srcDirs("../../engine/assets", gamePackage)
}
