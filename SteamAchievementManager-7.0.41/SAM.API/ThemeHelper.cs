using System;
using System.Drawing;
using System.Windows.Forms;

namespace SAM.API
{
    public static class ThemeHelper
    {
        public static readonly Color DarkBg = Color.FromArgb(10, 10, 20);
        public static readonly Color DarkSurface = Color.FromArgb(18, 18, 42);
        public static readonly Color MediumBg = Color.FromArgb(26, 26, 46);
        public static readonly Color PurpleBg = Color.FromArgb(45, 25, 78);
        public static readonly Color BrightPurple = Color.FromArgb(120, 80, 200);
        public static readonly Color LightText = Color.FromArgb(224, 224, 240);
        public static readonly Color AccentGreen = Color.FromArgb(72, 187, 120);
        public static readonly Color AccentRed = Color.FromArgb(220, 60, 60);
        public static readonly Color AccentOrange = Color.FromArgb(255, 165, 0);

        public static void Apply(Form form)
        {
            form.BackColor = DarkBg;
            form.ForeColor = LightText;
            foreach (Control c in form.Controls)
                ApplyToControl(c);
            form.Opacity = 0;
            var timer = new Timer { Interval = 15 };
            form.Tag = timer;
            timer.Tick += (s, e) =>
            {
                if (form.IsDisposed) { timer.Stop(); return; }
                form.Opacity = Math.Min(1.0, form.Opacity + 0.06);
                if (form.Opacity >= 1.0) timer.Stop();
            };
            timer.Start();
        }

        private static void ApplyToControl(Control c)
        {
            if (c is ToolStrip ts)
            {
                ts.BackColor = DarkSurface;
                ts.ForeColor = LightText;
                ts.Renderer = new PurpleRenderer();
                foreach (ToolStripItem i in ts.Items)
                {
                    i.BackColor = DarkSurface;
                    i.ForeColor = LightText;
                }
            }
            else if (c is StatusStrip ss)
            {
                ss.BackColor = DarkSurface;
                ss.ForeColor = LightText;
                ss.Renderer = new PurpleRenderer();
                foreach (ToolStripItem i in ss.Items)
                {
                    i.BackColor = DarkSurface;
                    i.ForeColor = LightText;
                }
            }
            else if (c is ListView lv)
            {
                lv.BackColor = Color.FromArgb(13, 20, 36);
                lv.ForeColor = LightText;
            }
            else if (c is TabControl tc)
            {
                tc.BackColor = DarkBg;
                tc.ForeColor = LightText;
            }
            else if (c is TabPage tp)
            {
                tp.BackColor = DarkBg;
                tp.ForeColor = LightText;
            }
            else if (c is DataGridView dgv)
            {
                dgv.BackgroundColor = DarkBg;
                dgv.ForeColor = LightText;
                dgv.ColumnHeadersDefaultCellStyle.BackColor = DarkSurface;
                dgv.ColumnHeadersDefaultCellStyle.ForeColor = LightText;
                dgv.RowsDefaultCellStyle.BackColor = DarkBg;
                dgv.RowsDefaultCellStyle.ForeColor = LightText;
                dgv.AlternatingRowsDefaultCellStyle.BackColor = MediumBg;
                dgv.GridColor = DarkSurface;
                dgv.EnableHeadersVisualStyles = false;
            }
            else if (c is CheckBox cb)
            {
                cb.ForeColor = LightText;
            }
            else if (c is Button b)
            {
                b.BackColor = PurpleBg;
                b.ForeColor = LightText;
                b.FlatStyle = FlatStyle.Flat;
                b.FlatAppearance.BorderColor = BrightPurple;
            }
            else if (c is TextBox tb)
            {
                tb.BackColor = DarkSurface;
                tb.ForeColor = LightText;
                tb.BorderStyle = BorderStyle.FixedSingle;
            }
            foreach (Control child in c.Controls)
                ApplyToControl(child);
        }
    }

    public class PurpleRenderer : ToolStripProfessionalRenderer
    {
        public PurpleRenderer() : base(new PurpleColors()) { }
    }

    public class PurpleColors : ProfessionalColorTable
    {
        public override Color MenuItemSelected => ThemeHelper.PurpleBg;
        public override Color MenuItemBorder => ThemeHelper.BrightPurple;
        public override Color ToolStripDropDownBackground => ThemeHelper.DarkSurface;
        public override Color ImageMarginGradientBegin => ThemeHelper.DarkSurface;
        public override Color ImageMarginGradientMiddle => ThemeHelper.DarkSurface;
        public override Color ImageMarginGradientEnd => ThemeHelper.DarkSurface;
        public override Color MenuBorder => ThemeHelper.BrightPurple;
        public override Color MenuItemSelectedGradientBegin => ThemeHelper.PurpleBg;
        public override Color MenuItemSelectedGradientEnd => ThemeHelper.PurpleBg;
        public override Color MenuItemPressedGradientBegin => ThemeHelper.MediumBg;
        public override Color MenuItemPressedGradientEnd => ThemeHelper.MediumBg;
        public override Color StatusStripGradientBegin => ThemeHelper.DarkSurface;
        public override Color StatusStripGradientEnd => ThemeHelper.DarkSurface;
        public override Color ToolStripGradientBegin => ThemeHelper.DarkSurface;
        public override Color ToolStripGradientEnd => ThemeHelper.DarkSurface;
        public override Color ToolStripBorder => ThemeHelper.DarkSurface;
    }
}
