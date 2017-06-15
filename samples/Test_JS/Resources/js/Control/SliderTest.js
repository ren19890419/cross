/**
 * Created by zhanglei on 16/8/3.
 */
var SliderTest = ca.CAViewController.extend({
    sliderNum : 1,
    sliderValue1: null,
    maxNum:1,
    ctor: function (num) {
        this._super();
        if(num != null){
            this.sliderNum = num;
        }
        this.initSliderTest();
    },
    viewDidLoad: function() {
    },
    initSliderTest:function(){


        this.sliderValue =  ca.CALabel.createWithLayout(ca.DLayout.set(ca.DHorizontalLayout_L_R(100, 100), ca.DVerticalLayout_T_H(300, 50)));
        this.sliderValue.setColor(ca.CAColor4B.set(51,204,255,255));
        this.sliderValue.setText("0");
        this.sliderValue.setFontSize(30);
        this.sliderValue.setTextAlignment(ca.CATextAlignment.Center);
        this.sliderValue.setVerticalTextAlignmet(ca.CAVerticalTextAlignment.Center);

        if (this.sliderNum == 0)
        {
            var view1 = ca.CAView.createWithLayout(ca.DLayoutFill);
            view1.setColor(ca.color._getGray());

            view1.addSubview(this.sliderValue);
            var slider1 = ca.CASlider.createWithLayout(ca.DLayout.set(ca.DHorizontalLayout_L_R(120, 120), ca.DVerticalLayout_T_H(500, 56)));
            var callbackSlider1 = this.sliderValue;
            slider1.setTarget(function (value) {
                value =  value * 100;
                callbackSlider1.setText(Math.round(value));
            });
            slider1.setTag(100);
            view1.addSubview(slider1);
            this.getView().addSubview(view1);
        }
        else if (this.sliderNum == 1)
        {
            var view2 =  ca.CAView.createWithLayout(ca.DLayoutFill);
            view2.setColor(ca.color._getGray());

            view2.addSubview(this.sliderValue);

            var slider2 =  ca.CASlider.createWithLayout(ca.DLayout.set(ca.DHorizontalLayout_L_R(120, 120), ca.DVerticalLayout_T_H(500, 56)));
            slider2.setTag(101);
            // slider2.setMaxTrackTintImage( ca.CAImage.create("source_material/ex1.png"));
            // slider2.setMinTrackTintImage( ca.CAImage.create("source_material/ex3.png"));
            slider2.setThumbTintImage( ca.CAImage.create("source_material/btn_square_highlighted.png"));
            var callbackSlider2 = this.sliderValue;
            slider2.setTarget(function (value) {
                value =  value * 100;
                callbackSlider2.setText(Math.round(value));
            });
            view2.addSubview(slider2);

            this.getView().addSubview(view2);
        }
    },
});
