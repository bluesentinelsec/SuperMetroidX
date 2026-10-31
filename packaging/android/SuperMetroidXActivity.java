package io.github.bluesentinelsec.SuperMetroidX;

import org.libsdl.app.SDLActivity;

/** SDL's activity, loading the one library that holds the game and SDL linked statically. */
public class SuperMetroidXActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"main"};
    }
}
