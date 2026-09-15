plugins {
    id("com.android.application")
}

android {
    namespace = "com.psonnera.xdripobb"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.psonnera.xdripobb"
        minSdk = 26
        targetSdk = 36
        versionCode = 2
        versionName = "1.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        viewBinding = true
    }

    // languages shipped in the APK: English (base) plus every values-<lang> folder; keep
    // this list and res/xml/locales_config.xml in step when adding a translation
    androidResources {
        localeFilters += listOf("en", "fr", "it")
    }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")

    testImplementation("junit:junit:4.13.2")
}
