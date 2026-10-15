package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Typeface;
import android.view.View;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Graphics settings for the Thor, kept in bbport.ini (run-bionic.sh reads them at start: scene
 *  size, upscaler, the game's effect patches). Other lines and comments of the file are kept. */
final class SettingsDialog {
    /** The outputs offered (output_res; 720p when missing) and the presets: the scene size is the output
     *  divided by the scale, rounded to even (run-bionic.sh and build_patch_parts.py use the same sizes). */
    private static final String[] OUTPUT_VALUES={"1920x1080","1280x720","854x480"};
    private static final String[] OUTPUT_NAMES={"1080p (1920×1080)","720p (1280×720)","480p (854×480)"};
    private static final int[][] OUTPUT_SIZES={{1920,1080},{1280,720},{854,480}};
    private static final int OUTPUT_DEFAULT=1;
    private static final String[] PRESET_NAMES={"Native","Quality","Balanced","Performance","Ultra performance"};
    private static final double[] PRESET_SCALES={1.0,1.5,1.7,2.0,3.0};
    private static int scene(int v,int preset) { return Math.max(2,(int)Math.rint(v/PRESET_SCALES[preset]/2)*2); }
    private static String presetName(int output,int preset) {
        return PRESET_NAMES[preset]+" ("+scene(OUTPUT_SIZES[output][0],preset)+"×"+scene(OUTPUT_SIZES[output][1],preset)+")";
    }
    private static final String[][] EFFECTS={
        {"effect_chromatic_aberration","Chromatic aberration","1"},
        {"effect_dof","Depth of field","1"},
        {"effect_motion_blur","Motion blur","1"},
        {"effect_ssao","Ambient occlusion (SSAO)","1"},
        {"effect_game_aa","Game's own anti-aliasing","1"},
        {"effect_dynamic_shadows","Shadows from dynamic lights","1"},
        {"effect_ssr","Screen-space reflections (not in the original)","0"},
    };
    // FSR 4 (INT8) is not offered: ~470 ms a frame and a black scene on the Adreno 740 (run-bionic.sh).
    /** Game settings: the game patches run-bionic.sh applies at start (patch-parts), as upstream's
     *  System menu pages and launcher offer them: {key, title, note, default}. */
    private static final String[][] GAME_PATCHES={
        {"skip_network_choice","Skip the online/offline screen","Start straight at the main menu, offline (there is no PSN here)","1"},
        {"skip_intro","Skip startup intros","The logos and intro movie at start","0"},
        {"debug_camera","Free camera","Toggle it with Cross + L3","0"},
        {"debug_menu","Debug menu","The game's own debug menu; needs its font files","0"},
    };
    /** Cheats: code hooks of the game applied at start (build_patch_parts.py, run-bionic.sh). */
    private static final String[][] CHEATS={
        {"cheat_health","Infinite health","Your HP refills to full every frame","0"},
        {"cheat_items","Infinite blood vials and items","Using a blood vial, bullet or other item leaves its count as it was "
            +"(items moved to storage or given away stay too)","0"},
    };
    private static final String[] UPSCALER_VALUES={"off","fsr3"};
    private static final String[] UPSCALER_NAMES={"Off","FSR 3.1"};
    private static final int FSR3=1;
    private static final String[] FPS_VALUES={"30","40","45","60","0"};
    private static final String[] FPS_NAMES={"30 (the game's own)","40","45","60","Unlimited"};
    /** Snapdragon 8 Gen 2: three Cortex-A510, four Cortex-A715, one Cortex-X3. */
    private static final String[] CORE_NAMES={"Core 0 · efficiency A510 2.0 GHz","Core 1 · efficiency A510 2.0 GHz",
        "Core 2 · efficiency A510 2.0 GHz","Core 3 · performance A715 2.8 GHz","Core 4 · performance A715 2.8 GHz",
        "Core 5 · performance A715 2.8 GHz","Core 6 · performance A715 2.8 GHz","Core 7 · prime X3 3.2 GHz"};
    private static final String[] LOD_VALUES={"-2","0","1","2"};
    private static final String[] LOD_NAMES={"Highest","Game default","Lower","Lowest"};

    private final Activity activity;
    private final File file;
    private final Runnable restart;
    private final Map<String,String> values=new LinkedHashMap<>();
    private final List<String> lines=new ArrayList<>();

    SettingsDialog(Activity activity,File file,Runnable restart) {
        this.activity=activity; this.file=file; this.restart=restart;
    }

    private int dp(float n) { return Math.round(n*activity.getResources().getDisplayMetrics().density); }
    private String get(String key,String fallback) { String v=values.get(key); return v==null || v.isEmpty() ? fallback : v; }

    private void load() {
        values.clear(); lines.clear();
        if (!file.isFile()) return;
        try (BufferedReader r=new BufferedReader(new InputStreamReader(new FileInputStream(file),"UTF-8"))) {
            for (String line; (line=r.readLine())!=null;) {
                lines.add(line);
                int eq=line.indexOf('=');
                if (eq>0 && !line.startsWith("#")) values.put(line.substring(0,eq).trim(),line.substring(eq+1).trim());
            }
        } catch (Exception ex) { android.util.Log.e("Bloodborne","settings: "+ex); }
    }
    /** Changed keys replace their lines; new keys are appended. Written whole, then renamed. */
    private boolean save(Map<String,String> changed) {
        List<String> out=new ArrayList<>();
        Map<String,String> pending=new LinkedHashMap<>(changed);
        for (String line:lines) {
            int eq=line.indexOf('=');
            String key=eq>0 && !line.startsWith("#") ? line.substring(0,eq).trim() : null;
            if (key!=null && pending.containsKey(key)) out.add(key+"="+pending.remove(key));
            else out.add(line);
        }
        for (Map.Entry<String,String> e:pending.entrySet()) out.add(e.getKey()+"="+e.getValue());
        File temporary=new File(file.getPath()+".tmp");
        try (Writer w=new OutputStreamWriter(new FileOutputStream(temporary),"UTF-8")) {
            for (String line:out) w.write(line+"\n");
        } catch (Exception ex) { android.util.Log.e("Bloodborne","settings: "+ex); return false; }
        return temporary.renameTo(file);
    }
    /** One setting, written at once (the GPU driver menu). Before the first start the file is
     *  the defaults (bbport.default.ini, as run-bionic.sh would copy it) plus this line. */
    boolean set(String key,String value) {
        File defaults=new File(file.getParentFile(),"bbport.default.ini");
        if (!file.isFile() && defaults.isFile())
            try { java.nio.file.Files.copy(defaults.toPath(),file.toPath()); }
            catch (Exception ex) { android.util.Log.e("Bloodborne","settings: "+ex); }
        load(); Map<String,String> changed=new LinkedHashMap<>(); changed.put(key,value); return save(changed);
    }

    private TextView heading(LinearLayout box,String title) {
        TextView t=new TextView(activity); t.setText(title); t.setTextSize(15);
        t.setTypeface(Typeface.DEFAULT,Typeface.BOLD); t.setPadding(0,dp(14),0,dp(4));
        box.addView(t); return t;
    }
    private Switch toggle(LinearLayout box,String title,boolean on) {
        Switch s=new Switch(activity); s.setText(title); s.setChecked(on); s.setTextSize(15);
        s.setPadding(0,dp(6),0,dp(6)); box.addView(s); return s;
    }
    private RadioGroup choices(LinearLayout box,String[] names,int selected) {
        RadioGroup g=new RadioGroup(activity);
        for (int i=0;i<names.length;++i) {
            RadioButton b=new RadioButton(activity); b.setId(View.generateViewId()); b.setText(names[i]); b.setTextSize(15);
            g.addView(b); if (i==selected) g.check(b.getId());
        }
        box.addView(g); return g;
    }
    private static int indexOf(String[] values,String value,int fallback) {
        for (int i=0;i<values.length;++i) if (values[i].equals(value)) return i;
        return fallback;
    }
    /** A taskset list ("0-2,5", "3-7"); empty: every core. */
    private static boolean[] parseCpus(String list,int count) {
        boolean[] on=new boolean[count];
        if (list.trim().isEmpty()) { java.util.Arrays.fill(on,true); return on; }
        for (String part:list.split(",")) {
            try {
                String[] range=part.trim().split("-");
                int from=Integer.parseInt(range[0].trim()), to=range.length>1 ? Integer.parseInt(range[1].trim()) : from;
                for (int i=Math.max(0,from);i<=Math.min(count-1,to);++i) on[i]=true;
            } catch (NumberFormatException ignored) {}
        }
        boolean any=false; for (boolean b:on) any|=b;
        if (!any) java.util.Arrays.fill(on,true);
        return on;
    }
    private static String cpuList(Switch[] cores) {
        StringBuilder list=new StringBuilder(); boolean all=true;
        for (int i=0;i<cores.length;++i) {
            if (!cores[i].isChecked()) { all=false; continue; }
            if (list.length()>0) list.append(',');
            list.append(i);
        }
        return all ? "" : list.toString();
    }
    private static int checkedIndex(RadioGroup g) {
        for (int i=0;i<g.getChildCount();++i) if (((RadioButton)g.getChildAt(i)).isChecked()) return i;
        return -1;
    }

    void show() {
        load();
        LinearLayout box=new LinearLayout(activity); box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24),dp(4),dp(24),dp(8));

        heading(box,"Output resolution");
        final RadioGroup outputs=choices(box,OUTPUT_NAMES,indexOf(OUTPUT_VALUES,get("output_res",""),OUTPUT_DEFAULT));
        TextView outputNote=new TextView(activity); outputNote.setTextSize(13); outputNote.setAlpha(0.7f);
        outputNote.setText("Higher output looks sharper but costs more GPU time; the upscaler renders the scene smaller and fills it.");
        box.addView(outputNote);
        heading(box,"Upscaling");
        final RadioGroup upscalers=choices(box,UPSCALER_NAMES,indexOf(UPSCALER_VALUES,get("upscaler","fsr3"),1));
        heading(box,"Render resolution");
        int preset=0;
        try { preset=Math.max(0,Math.min(4,Integer.parseInt(get("preset","3")))); } catch (NumberFormatException ignored) { preset=3; }
        final RadioGroup presets=choices(box,PRESET_NAMES,preset);
        final Switch frameGen=toggle(box,"FSR 3.1 frame generation","1".equals(get("frame_generation","0")));
        TextView frameGenNote=new TextView(activity); frameGenNote.setTextSize(13); frameGenNote.setAlpha(0.7f);
        frameGenNote.setText("Shows a generated frame between each two (about twice the frames on screen); "
            +"adds about half a frame of input delay. Best with a steady frame rate.");
        box.addView(frameGenNote);
        final TextView presetNote=new TextView(activity); presetNote.setTextSize(13); presetNote.setAlpha(0.7f);
        box.addView(presetNote);
        RadioGroup.OnCheckedChangeListener upscaling=(g,id) -> {
            int upscaler=checkedIndex(upscalers), output=Math.max(0,checkedIndex(outputs));
            boolean on=upscaler!=0;
            ((RadioButton)upscalers.getChildAt(0)).setText("Off (render at "+OUTPUT_SIZES[output][0]+"×"+OUTPUT_SIZES[output][1]+")");
            for (int i=0;i<presets.getChildCount();++i) ((RadioButton)presets.getChildAt(i)).setText(presetName(output,i));
            for (int i=0;i<presets.getChildCount();++i) presets.getChildAt(i).setEnabled(on);
            frameGen.setEnabled(upscaler==FSR3);
            presetNote.setText(on ? "Lower is faster; the upscaler fills the screen." : "Without upscaling the game renders at the output resolution.");
        };
        upscalers.setOnCheckedChangeListener(upscaling); outputs.setOnCheckedChangeListener(upscaling);
        upscaling.onCheckedChanged(upscalers,0);

        heading(box,"Image");
        final Switch sharpen=toggle(box,"Sharpening","1".equals(get("sharpen","0")));
        final TextView sharpLabel=new TextView(activity); sharpLabel.setTextSize(14); box.addView(sharpLabel);
        final SeekBar sharpness=new SeekBar(activity); sharpness.setMax(100);
        float initial=0.5f;
        try { initial=Float.parseFloat(get("sharpness","0.5")); } catch (NumberFormatException ignored) {}
        sharpness.setProgress(Math.round(Math.max(0,Math.min(1,initial))*100));
        SeekBar.OnSeekBarChangeListener sharpText=new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s,int p,boolean user) { sharpLabel.setText("Sharpness: "+p+"%"); }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        };
        sharpness.setOnSeekBarChangeListener(sharpText); sharpText.onProgressChanged(sharpness,sharpness.getProgress(),false);
        box.addView(sharpness);
        sharpen.setOnCheckedChangeListener((b,on) -> sharpness.setEnabled(on)); sharpness.setEnabled(sharpen.isChecked());
        final Switch fps=toggle(box,"Show FPS","1".equals(get("show_fps","0")));

        heading(box,"FPS limit");
        final RadioGroup fpsLimit=choices(box,FPS_NAMES,indexOf(FPS_VALUES,get("fps_limit","30"),0));
        TextView fpsNote=new TextView(activity); fpsNote.setTextSize(13); fpsNote.setAlpha(0.7f);
        fpsNote.setText("Above 30 uses the community frame-rate patch (game timing follows the real frame time).");
        box.addView(fpsNote);
        heading(box,"VSync");
        final Switch vsync=toggle(box,"VSync","1".equals(get("vsync","0")));
        TextView vsyncNote=new TextView(activity); vsyncNote.setTextSize(13); vsyncNote.setAlpha(0.7f);
        vsyncNote.setText("Every frame is shown for at least one screen refresh: smoother pacing, a little more latency. "
            +"Off: the newest frame is shown, frames finished within the same refresh are skipped.");
        box.addView(vsyncNote);

        heading(box,"CPU cores the game may use");
        final Switch[] cores=new Switch[CORE_NAMES.length];
        boolean[] allowed=parseCpus(get("thor_cpus",""),CORE_NAMES.length);
        for (int i=0;i<cores.length;++i) cores[i]=toggle(box,CORE_NAMES[i],allowed[i]);
        CompoundButton.OnCheckedChangeListener keepOne=(b,on) -> {
            if (on) return;
            for (Switch c:cores) if (c.isChecked()) return;
            b.setChecked(true); // at least one core
        };
        for (Switch c:cores) c.setOnCheckedChangeListener(keepOne);

        heading(box,"Game effects");
        final Switch[] effects=new Switch[EFFECTS.length];
        for (int i=0;i<EFFECTS.length;++i) effects[i]=toggle(box,EFFECTS[i][1],"1".equals(get(EFFECTS[i][0],EFFECTS[i][2])));

        heading(box,"Model detail");
        int lod=1;
        for (int i=0;i<LOD_VALUES.length;++i) if (LOD_VALUES[i].equals(get("model_lod","0"))) lod=i;
        final RadioGroup lods=choices(box,LOD_NAMES,lod);

        ScrollView scroll=new ScrollView(activity); scroll.addView(box);
        new AlertDialog.Builder(activity).setTitle("Graphics settings").setView(scroll)
            .setNegativeButton("Cancel",null)
            .setPositiveButton("Apply & restart",(d,w) -> {
                Map<String,String> changed=new LinkedHashMap<>();
                changed.put("output_res",OUTPUT_VALUES[Math.max(0,checkedIndex(outputs))]);
                changed.put("upscaler",UPSCALER_VALUES[Math.max(0,checkedIndex(upscalers))]);
                changed.put("fps_limit",FPS_VALUES[Math.max(0,checkedIndex(fpsLimit))]);
                changed.put("frame_generation",frameGen.isChecked() && checkedIndex(upscalers)!=0 ? "1" : "0");
                changed.put("vsync",vsync.isChecked() ? "1" : "0");
                changed.put("thor_cpus",cpuList(cores));
                changed.put("preset",String.valueOf(Math.max(0,checkedIndex(presets))));
                changed.put("sharpen",sharpen.isChecked() ? "1" : "0");
                changed.put("sharpness",String.format(Locale.ROOT,"%.2f",sharpness.getProgress()/100f));
                changed.put("show_fps",fps.isChecked() ? "1" : "0");
                for (int i=0;i<EFFECTS.length;++i) changed.put(EFFECTS[i][0],effects[i].isChecked() ? "1" : "0");
                changed.put("model_lod",LOD_VALUES[Math.max(0,checkedIndex(lods))]);
                apply(changed);
            }).show();
    }

    /** Saves the changed keys and restarts the game when any differs from the file. */
    private void apply(Map<String,String> changed) {
        boolean same=true;
        for (Map.Entry<String,String> e:changed.entrySet()) if (!e.getValue().equals(values.get(e.getKey()))) same=false;
        if (same) return;
        if (save(changed)) restart.run();
        else new AlertDialog.Builder(activity).setMessage("The settings could not be saved.").setPositiveButton("OK",null).show();
    }

    /** Game settings: the game's patches (applied when the game starts: Apply restarts it). */
    void showGame() {
        load();
        LinearLayout box=new LinearLayout(activity); box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24),dp(4),dp(24),dp(8));
        final Switch[] toggles=new Switch[GAME_PATCHES.length];
        for (int i=0;i<GAME_PATCHES.length;++i) {
            toggles[i]=toggle(box,GAME_PATCHES[i][1],"1".equals(get(GAME_PATCHES[i][0],GAME_PATCHES[i][3])));
            TextView note=new TextView(activity); note.setTextSize(12); note.setAlpha(0.65f);
            note.setText(GAME_PATCHES[i][2]); note.setPadding(0,0,0,dp(6));
            box.addView(note);
        }
        TextView restartNote=new TextView(activity); restartNote.setTextSize(13); restartNote.setAlpha(0.7f);
        restartNote.setText("These are patches to the game: Apply restarts it (unsaved progress is lost).");
        restartNote.setPadding(0,dp(8),0,0);
        box.addView(restartNote);
        ScrollView scroll=new ScrollView(activity); scroll.addView(box);
        new AlertDialog.Builder(activity).setTitle("Game settings").setView(scroll)
            .setNegativeButton("Cancel",null)
            .setPositiveButton("Apply",(d,w) -> {
                Map<String,String> changed=new LinkedHashMap<>();
                for (int i=0;i<GAME_PATCHES.length;++i) changed.put(GAME_PATCHES[i][0],toggles[i].isChecked() ? "1" : "0");
                apply(changed);
            }).show();
    }

    /** Cheats (applied when the game starts: Apply restarts it). */
    void showCheats() {
        load();
        LinearLayout box=new LinearLayout(activity); box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24),dp(4),dp(24),dp(8));
        final Switch[] toggles=new Switch[CHEATS.length];
        for (int i=0;i<CHEATS.length;++i) {
            toggles[i]=toggle(box,CHEATS[i][1],"1".equals(get(CHEATS[i][0],CHEATS[i][3])));
            TextView note=new TextView(activity); note.setTextSize(12); note.setAlpha(0.65f);
            note.setText(CHEATS[i][2]); note.setPadding(0,0,0,dp(6));
            box.addView(note);
        }
        TextView restartNote=new TextView(activity); restartNote.setTextSize(13); restartNote.setAlpha(0.7f);
        restartNote.setText("For Bloodborne 1.09 offline play. They switch at once, while you play.");
        restartNote.setPadding(0,dp(8),0,0);
        box.addView(restartNote);
        ScrollView scroll=new ScrollView(activity); scroll.addView(box);
        new AlertDialog.Builder(activity).setTitle("Cheats").setView(scroll)
            .setNegativeButton("Cancel",null)
            .setPositiveButton("Apply",(d,w) -> {
                Map<String,String> changed=new LinkedHashMap<>();
                for (int i=0;i<CHEATS.length;++i) changed.put(CHEATS[i][0],toggles[i].isChecked() ? "1" : "0");
                // The running game follows cheats.state (probe.c reads it): no restart.
                boolean ok=save(changed) && writeCheatState(new File(file.getParentFile(),"cheats.state"),
                    toggles[0].isChecked(),toggles[1].isChecked());
                android.widget.Toast.makeText(activity,ok ? "Cheats applied" : "The cheats could not be saved",
                    android.widget.Toast.LENGTH_SHORT).show();
            }).show();
    }

    /** "10": infinite health on, items off (run-bionic.sh's BB_CHEATS_FILE), written whole then renamed. */
    private static boolean writeCheatState(File state,boolean health,boolean items) {
        File temporary=new File(state.getPath()+".tmp");
        try (Writer w=new OutputStreamWriter(new FileOutputStream(temporary),"UTF-8")) {
            w.write((health ? "1" : "0")+(items ? "1" : "0")+"\n");
        } catch (Exception ex) { android.util.Log.e("Bloodborne","cheats: "+ex); return false; }
        return temporary.renameTo(state);
    }
}
